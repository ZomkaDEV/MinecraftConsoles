#include "stdafx.h"

#include "CliCommandBan.h"

#include "..\ServerCliEngine.h"
#include "..\ServerCliParser.h"
#include "..\..\Access\Access.h"
#include "..\..\Common\StringUtils.h"
#include "..\..\..\Minecraft.Client\PlayerConnection.h"
#include "..\..\..\Minecraft.Client\ServerPlayer.h"
#include "..\..\..\Minecraft.World\DisconnectPacket.h"

#include <algorithm>

namespace ServerRuntime
{
	namespace
	{
		static std::string JoinTokens(const std::vector<std::string> &tokens, size_t startIndex)
		{
			std::string joined;
			for (size_t i = startIndex; i < tokens.size(); ++i)
			{
				if (!joined.empty())
				{
					joined.push_back(' ');
				}
				joined += tokens[i];
			}
			return joined;
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

		static void CollectPlayerBanXuids(const std::shared_ptr<ServerPlayer> &player, std::vector<PlayerUID> *out)
		{
			if (player == NULL || out == NULL)
			{
				return;
			}

			// Keep both identity variants because the dedicated server checks login and online XUIDs separately.
			AppendUniqueXuid(player->getXuid(), out);
			AppendUniqueXuid(player->getOnlineXuid(), out);
		}
	}

	const char *CliCommandBan::Name() const
	{
		return "ban";
	}

	const char *CliCommandBan::Usage() const
	{
		return "ban <player> [reason ...]";
	}

	const char *CliCommandBan::Description() const
	{
		return "Ban an online player.";
	}

	/**
	 * Resolves the live player, writes one or more Access ban entries, and disconnects the target with the banned reason
	 * 対象プレイヤーを解決してBANを保存し切断する
	 */
	bool CliCommandBan::Execute(const ServerCliParsedLine &line, ServerCliEngine *engine)
	{
		if (line.tokens.size() < 2)
		{
			engine->LogWarn("Usage: ban <player> [reason ...]");
			return false;
		}
		if (!ServerRuntime::Access::IsInitialized())
		{
			engine->LogWarn("Access manager is not initialized.");
			return false;
		}

		std::shared_ptr<ServerPlayer> target = engine->FindPlayerByNameUtf8(line.tokens[1]);
		if (target == NULL)
		{
			engine->LogWarn("Unknown player: " + line.tokens[1] + " (this server build can only ban players that are currently online).");
			return false;
		}

		std::vector<PlayerUID> xuids;
		CollectPlayerBanXuids(target, &xuids);
		if (xuids.empty())
		{
			engine->LogWarn("Cannot ban that player because no valid XUID is available.");
			return false;
		}

		bool hasUnbannedIdentity = false;
		for (size_t i = 0; i < xuids.size(); ++i)
		{
			if (!ServerRuntime::Access::IsPlayerBanned(xuids[i]))
			{
				hasUnbannedIdentity = true;
				break;
			}
		}
		if (!hasUnbannedIdentity)
		{
			engine->LogWarn("That player is already banned.");
			return false;
		}

		ServerRuntime::Access::BanMetadata metadata = ServerRuntime::Access::BanManager::BuildDefaultMetadata("Console");
		metadata.reason = JoinTokens(line.tokens, 2);
		if (metadata.reason.empty())
		{
			metadata.reason = "Banned by an operator.";
		}

		const std::string playerName = StringUtils::WideToUtf8(target->getName());
		for (size_t i = 0; i < xuids.size(); ++i)
		{
			if (ServerRuntime::Access::IsPlayerBanned(xuids[i]))
			{
				continue;
			}

			if (!ServerRuntime::Access::AddPlayerBan(xuids[i], playerName, metadata))
			{
				engine->LogError("Failed to write player ban.");
				return false;
			}
		}

		if (target->connection != NULL)
		{
			target->connection->disconnect(DisconnectPacket::eDisconnect_Banned);
		}

		engine->LogInfo("Banned player " + playerName + ".");
		return true;
	}

	/**
	 * Suggests currently connected player names for the Java-style player argument
	 * プレイヤー引数の補完候補を返す
	 */
	void CliCommandBan::Complete(const ServerCliCompletionContext &context, const ServerCliEngine *engine, std::vector<std::string> *out) const
	{
		if (context.currentTokenIndex == 1)
		{
			engine->SuggestPlayers(context.prefix, context.linePrefix, out);
		}
	}
}
