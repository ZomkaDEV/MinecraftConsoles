#include "stdafx.h"

#include "CliCommandPardon.h"

#include "..\ServerCliEngine.h"
#include "..\ServerCliParser.h"
#include "..\..\Access\Access.h"
#include "..\..\Common\StringUtils.h"
#include "..\..\..\Minecraft.Client\ServerPlayer.h"

#include <algorithm>

namespace ServerRuntime
{
	namespace
	{
		static void AppendUniqueText(const std::string &text, std::vector<std::string> *out)
		{
			if (out == NULL || text.empty())
			{
				return;
			}

			if (std::find(out->begin(), out->end(), text) == out->end())
			{
				out->push_back(text);
			}
		}

		static void AppendUniqueXuid(PlayerUID xuid, std::vector<PlayerUID> *out)
		{
			if (out == NULL || xuid == INVALID_XUID)
			{
				return;
			}

			if (std::find(out->begin(), out->end(), xuid) == out->end())
			{
				out->push_back(xuid);
			}
		}
	}

	const char *CliCommandPardon::Name() const
	{
		return "pardon";
	}

	const char *CliCommandPardon::Usage() const
	{
		return "pardon <player>";
	}

	const char *CliCommandPardon::Description() const
	{
		return "Remove a player ban.";
	}

	/**
	 * Removes every Access ban entry that matches the requested player name so dual-XUID entries are cleared together
	 * 名前に一致するBANをまとめて解除する
	 */
	bool CliCommandPardon::Execute(const ServerCliParsedLine &line, ServerCliEngine *engine)
	{
		if (line.tokens.size() != 2)
		{
			engine->LogWarn("Usage: pardon <player>");
			return false;
		}
		if (!ServerRuntime::Access::IsInitialized())
		{
			engine->LogWarn("Access manager is not initialized.");
			return false;
		}

		std::vector<PlayerUID> xuidsToRemove;
		std::vector<std::string> matchedNames;
		std::shared_ptr<ServerPlayer> onlineTarget = engine->FindPlayerByNameUtf8(line.tokens[1]);
		if (onlineTarget != NULL)
		{
			if (ServerRuntime::Access::IsPlayerBanned(onlineTarget->getXuid()))
			{
				AppendUniqueXuid(onlineTarget->getXuid(), &xuidsToRemove);
			}
			if (ServerRuntime::Access::IsPlayerBanned(onlineTarget->getOnlineXuid()))
			{
				AppendUniqueXuid(onlineTarget->getOnlineXuid(), &xuidsToRemove);
			}
		}

		std::vector<ServerRuntime::Access::BannedPlayerEntry> entries;
		if (!ServerRuntime::Access::SnapshotBannedPlayers(&entries))
		{
			engine->LogError("Failed to read banned players.");
			return false;
		}

		const std::string loweredTarget = StringUtils::ToLowerAscii(line.tokens[1]);
		for (size_t i = 0; i < entries.size(); ++i)
		{
			if (StringUtils::ToLowerAscii(entries[i].name) == loweredTarget)
			{
				unsigned long long numericXuid = _strtoui64(entries[i].xuid.c_str(), NULL, 0);
				if (numericXuid != 0ULL)
				{
					AppendUniqueXuid((PlayerUID)numericXuid, &xuidsToRemove);
				}
				AppendUniqueText(entries[i].name, &matchedNames);
			}
		}

		if (xuidsToRemove.empty())
		{
			engine->LogWarn("That player is not banned.");
			return false;
		}

		for (size_t i = 0; i < xuidsToRemove.size(); ++i)
		{
			if (!ServerRuntime::Access::RemovePlayerBan(xuidsToRemove[i]))
			{
				engine->LogError("Failed to remove player ban.");
				return false;
			}
		}

		std::string playerName = line.tokens[1];
		if (!matchedNames.empty())
		{
			playerName = matchedNames[0];
		}
		else if (onlineTarget != NULL)
		{
			playerName = StringUtils::WideToUtf8(onlineTarget->getName());
		}

		engine->LogInfo("Unbanned player " + playerName + ".");
		return true;
	}

	/**
	 * Suggests currently banned player names first and then online names for convenience
	 * BAN済み名とオンライン名を補完候補に出す
	 */
	void CliCommandPardon::Complete(const ServerCliCompletionContext &context, const ServerCliEngine *engine, std::vector<std::string> *out) const
	{
		if (context.currentTokenIndex != 1 || out == NULL)
		{
			return;
		}

		std::vector<ServerRuntime::Access::BannedPlayerEntry> entries;
		if (ServerRuntime::Access::SnapshotBannedPlayers(&entries))
		{
			const std::string loweredPrefix = StringUtils::ToLowerAscii(context.prefix);
			std::vector<std::string> names;
			for (size_t i = 0; i < entries.size(); ++i)
			{
				AppendUniqueText(entries[i].name, &names);
			}

			for (size_t i = 0; i < names.size(); ++i)
			{
				if (StringUtils::ToLowerAscii(names[i]).compare(0, loweredPrefix.size(), loweredPrefix) == 0)
				{
					out->push_back(context.linePrefix + names[i]);
				}
			}
		}

		engine->SuggestPlayers(context.prefix, context.linePrefix, out);
	}
}
