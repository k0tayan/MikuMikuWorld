#include "UscSerializer.h"
#include "Constants.h"
#include "IO.h"
#include "File.h"
#include "JsonIO.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

using json = nlohmann::json;

namespace MikuMikuWorld
{
	static int toTick(double beat)
	{
		return static_cast<int>(std::lround(beat * TICKS_PER_BEAT));
	}

	static int toWidth(double size)
	{
		int w = static_cast<int>(std::lround(size * 2.0));
		return std::clamp(w, MIN_NOTE_WIDTH, MAX_NOTE_WIDTH);
	}

	static int toLane(double uscLane, double size, int width)
	{
		int lane = static_cast<int>(std::lround(uscLane + 6.0 - size));
		return std::clamp(lane, MIN_LANE, MAX_LANE - width + 1);
	}

	static EaseType toEase(const std::string& name)
	{
		if (name == "in") return EaseType::EaseIn;
		if (name == "out") return EaseType::EaseOut;
		// USC "inout" / "outin" have no native equivalent; fall back to Linear.
		return EaseType::Linear;
	}

	static FlickType toFlick(const std::string& direction)
	{
		if (direction == "up") return FlickType::Default;
		if (direction == "left") return FlickType::Left;
		if (direction == "right") return FlickType::Right;
		return FlickType::None;
	}

	void UscSerializer::serialize(const Score& score, std::string filename)
	{
		throw std::runtime_error("Writing USC is not supported");
	}

	Score UscSerializer::deserialize(std::string filename)
	{
		IO::File file(filename, IO::FileMode::Read);
		std::string text = file.readAllText();
		file.close();

		json vusc = json::parse(text);
		int version = jsonIO::tryGetValue<int>(vusc, "version", 0);
		if (version != 2)
			throw std::runtime_error(IO::formatString("Unsupported USC version: %d", version));

		const json& usc = vusc.at("usc");

		Score score;
		score.tempoChanges.clear();
		score.hiSpeedChanges.clear();
		score.metadata.musicOffset = jsonIO::tryGetValue<float>(usc, "offset", 0.0f) * -1000.0f;

		if (!usc.contains("objects") || !usc["objects"].is_array())
			throw std::runtime_error("USC is missing objects array");

		for (const auto& obj : usc["objects"])
		{
			const std::string type = jsonIO::tryGetValue<std::string>(obj, "type", "");

			if (type == "bpm")
			{
				score.tempoChanges.push_back(Tempo{
					toTick(obj.at("beat").get<double>()),
					obj.at("bpm").get<float>() });
			}
			else if (type == "timeScaleGroup")
			{
				// Native model has a single hi-speed track; flatten all groups into it.
				if (obj.contains("changes") && obj["changes"].is_array())
				{
					for (const auto& change : obj["changes"])
					{
						score.hiSpeedChanges.push_back(HiSpeedChange{
							toTick(change.at("beat").get<double>()),
							change.at("timeScale").get<float>() });
					}
				}
			}
			else if (type == "single")
			{
				Note note(NoteType::Tap);
				note.width = toWidth(obj.at("size").get<double>());
				note.lane = toLane(obj.at("lane").get<double>(), obj.at("size").get<double>(), note.width);
				note.tick = toTick(obj.at("beat").get<double>());
				note.critical = jsonIO::tryGetValue<bool>(obj, "critical", false);
				note.friction = jsonIO::tryGetValue<bool>(obj, "trace", false);
				note.flick = toFlick(jsonIO::tryGetValue<std::string>(obj, "direction", ""));
				note.ID = nextID++;
				score.notes[note.ID] = note;
			}
			else if (type == "damage")
			{
				// Damage notes are not representable in this editor; skip.
				continue;
			}
			else if (type == "guide")
			{
				HoldNote hold;
				hold.startType = HoldNoteType::Guide;
				hold.endType = HoldNoteType::Guide;

				if (!obj.contains("midpoints") || !obj["midpoints"].is_array() || obj["midpoints"].size() < 2)
					continue;

				// USC guides have 8 colors; mmw's data model only distinguishes
				// critical (yellow) from non-critical. Map yellow to critical so
				// the preview renders yellow guides as yellow; other colors fall
				// back to the non-critical (green) base.
				const bool isCritical =
					jsonIO::tryGetValue<std::string>(obj, "color", "") == "yellow";

				const auto& midpoints = obj["midpoints"];
				const size_t count = midpoints.size();

				for (size_t i = 0; i < count; ++i)
				{
					const auto& step = midpoints[i];
					double size = step.at("size").get<double>();
					int width = toWidth(size);
					int lane = toLane(step.at("lane").get<double>(), size, width);
					int tick = toTick(step.at("beat").get<double>());
					EaseType ease = toEase(jsonIO::tryGetValue<std::string>(step, "ease", "linear"));

					if (i == 0)
					{
						Note n(NoteType::Hold, tick, lane, width);
						n.critical = isCritical;
						n.ID = nextID++;
						score.notes[n.ID] = n;
						hold.start = HoldStep{ n.ID, HoldStepType::Normal, ease };
					}
					else if (i == count - 1)
					{
						Note n(NoteType::HoldEnd, tick, lane, width);
						n.critical = isCritical;
						n.ID = nextID++;
						n.parentID = hold.start.ID;
						score.notes[n.ID] = n;
						hold.end = n.ID;
					}
					else
					{
						Note n(NoteType::HoldMid, tick, lane, width);
						n.critical = isCritical;
						n.ID = nextID++;
						n.parentID = hold.start.ID;
						score.notes[n.ID] = n;
						hold.steps.push_back(HoldStep{ n.ID, HoldStepType::Hidden, ease });
					}
				}

				score.holdNotes[hold.start.ID] = hold;
			}
			else if (type == "slide")
			{
				if (!obj.contains("connections") || !obj["connections"].is_array())
					continue;

				std::vector<json> connections = obj["connections"].get<std::vector<json>>();
				// Ensure start first, end last, middle entries keep input order.
				std::stable_sort(connections.begin(), connections.end(),
					[](const json& a, const json& b)
					{
						const std::string ta = jsonIO::tryGetValue<std::string>(a, "type", "");
						const std::string tb = jsonIO::tryGetValue<std::string>(b, "type", "");
						auto rank = [](const std::string& t)
						{
							if (t == "start") return 0;
							if (t == "end") return 2;
							return 1;
						};
						return rank(ta) < rank(tb);
					});

				HoldNote hold;
				bool isCritical = false;

				for (const auto& step : connections)
				{
					const std::string stepType = jsonIO::tryGetValue<std::string>(step, "type", "");
					double size = step.at("size").get<double>();
					int width = toWidth(size);
					int lane = toLane(step.at("lane").get<double>(), size, width);
					int tick = toTick(step.at("beat").get<double>());
					EaseType ease = toEase(jsonIO::tryGetValue<std::string>(step, "ease", "linear"));
					const std::string judge = jsonIO::tryGetValue<std::string>(step, "judgeType", "normal");

					if (stepType == "start")
					{
						Note n(NoteType::Hold, tick, lane, width);
						n.critical = jsonIO::tryGetValue<bool>(step, "critical", false);
						isCritical = n.critical;
						if (judge == "trace")
						{
							n.friction = true;
							hold.startType = HoldNoteType::Normal;
						}
						else if (judge == "none")
						{
							hold.startType = HoldNoteType::Hidden;
						}
						else
						{
							hold.startType = HoldNoteType::Normal;
						}
						n.ID = nextID++;
						score.notes[n.ID] = n;
						hold.start = HoldStep{ n.ID, HoldStepType::Normal, ease };
					}
					else if (stepType == "end")
					{
						Note n(NoteType::HoldEnd, tick, lane, width);
						n.critical = isCritical || jsonIO::tryGetValue<bool>(step, "critical", false);
						n.flick = toFlick(jsonIO::tryGetValue<std::string>(step, "direction", ""));
						if (judge == "trace")
						{
							n.friction = true;
							hold.endType = HoldNoteType::Normal;
						}
						else if (judge == "none")
						{
							hold.endType = HoldNoteType::Hidden;
						}
						else
						{
							hold.endType = HoldNoteType::Normal;
						}
						n.ID = nextID++;
						n.parentID = hold.start.ID;
						score.notes[n.ID] = n;
						hold.end = n.ID;
					}
					else
					{
						Note n(NoteType::HoldMid, tick, lane, width);
						n.critical = isCritical;
						n.ID = nextID++;
						n.parentID = hold.start.ID;
						score.notes[n.ID] = n;

						HoldStepType midType;
						if (stepType == "attach")
						{
							midType = HoldStepType::Skip;
						}
						else if (step.contains("critical"))
						{
							midType = HoldStepType::Normal;
						}
						else
						{
							midType = HoldStepType::Hidden;
						}
						hold.steps.push_back(HoldStep{ n.ID, midType, ease });
					}
				}

				if (hold.start.ID == 0 || hold.end == 0)
					continue;

				sortHoldSteps(score, hold);
				score.holdNotes[hold.start.ID] = hold;
			}
		}

		if (score.tempoChanges.empty())
			score.tempoChanges.push_back(Tempo(0, 120));

		std::sort(score.tempoChanges.begin(), score.tempoChanges.end(),
			[](const Tempo& a, const Tempo& b) { return a.tick < b.tick; });
		std::sort(score.hiSpeedChanges.begin(), score.hiSpeedChanges.end(),
			[](const HiSpeedChange& a, const HiSpeedChange& b) { return a.tick < b.tick; });

		return score;
	}
}
