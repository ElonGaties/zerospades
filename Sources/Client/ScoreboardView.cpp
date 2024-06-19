/*
 Copyright (c) 2013 yvt

 This file is part of OpenSpades.

 OpenSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 OpenSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with OpenSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "CTFGameMode.h"
#include "Client.h"
#include "Fonts.h"
#include "IFont.h"
#include "IImage.h"
#include "IRenderer.h"
#include "MapView.h"
#include "NetClient.h"
#include "Player.h"
#include "ScoreboardView.h"
#include "TCGameMode.h"
#include "World.h"
#include <Core/Debug.h>
#include <Core/Settings.h>
#include <Core/Strings.h>

SPADES_SETTING(cg_minimapPlayerColor);

namespace spades {
	namespace client {

		static const Vector4 white = {1, 1, 1, 1};
		static const Vector4 spectatorIdColor = {210.0F / 255, 210.0F / 255, 210.0F / 255, 1}; // Grey
		static const Vector4 spectatorTextColor = {220.0F / 255, 220.0F / 255, 0, 1}; // Goldish yellow
		static const int spectatorTeamId = 255; // Spectators have a team id of 255

		ScoreboardView::ScoreboardView(Client* client)
		    : client(client), renderer(client->GetRenderer()) {
			SPADES_MARK_FUNCTION();
			world = nullptr;
			tc = nullptr;
			ctf = nullptr;

			// Use GUI font if spectator string has special chars
			auto spectatorString = _TrN("Client", "Spectator{1}", "Spectators{1}", "", "");
			auto hasSpecialChar =
			  std::find_if(spectatorString.begin(), spectatorString.end(), [](char ch) {
				  return !(isalnum(static_cast<unsigned char>(ch)) || ch == '_');
			  }) != spectatorString.end();

			spectatorFont = hasSpecialChar
				? client->fontManager->GetMediumFont()
				: client->fontManager->GetSquareDesignFont();

			intelIcon = renderer.RegisterImage("Gfx/Map/Intel.png");
		}

		ScoreboardView::~ScoreboardView() {}

		int ScoreboardView::GetTeamScore(int team) const {
			if (ctf) {
				return ctf->GetTeam(team).score;
			} else if (tc) {
				int num = 0;
				for (int i = 0; i < tc->GetNumTerritories(); i++) {
					if (tc->GetTerritory(i).ownerTeamId == team)
						num++;
				}
				return num;
			} else {
				return 0;
			}
		}

		Vector4 ScoreboardView::GetTeamColor(int team) {
			return ConvertColorRGBA(world->GetTeamColor(team));
		}

		void ScoreboardView::Draw() {
			SPADES_MARK_FUNCTION();

			world = client->GetWorld();
			if (!world)
				return; // no world

			// TODO: `ctf` and `tc` are only valid throughout the
			// method call's duration. Move them to a new context type
			stmp::optional<IGameMode&> mode = world->GetMode();
			ctf = (mode->ModeType() == IGameMode::m_CTF)
				? dynamic_cast<CTFGameMode*>(mode.get_pointer())
				: NULL;
			tc = (mode->ModeType() == IGameMode::m_TC)
				? dynamic_cast<TCGameMode*>(mode.get_pointer())
				: NULL;

			Handle<IImage> img;
			IFont& font = client->fontManager->GetSquareDesignFont();
			Vector2 pos, size;
			std::string str;

			float sw = renderer.ScreenWidth();
			float sh = renderer.ScreenHeight();

			float contentsWidth = sw + 8.0F;
			float maxContentsWidth = 800.0F;
			if (contentsWidth > maxContentsWidth)
				contentsWidth = maxContentsWidth;

			float contentsHeight = sh - 156.0F;
			float maxContentsHeight = 360.0F;
			if (contentsHeight > maxContentsHeight)
				contentsHeight = maxContentsHeight;

			float teamBarTop = (sh - contentsHeight) * 0.5F;
			float teamBarHeight = 60.0F;
			float contentsLeft = (sw - contentsWidth) * 0.5F;
			float contentsRight = contentsLeft + contentsWidth;
			float playersHeight = 300.0F - teamBarHeight;
			float playersTop = teamBarTop + teamBarHeight;
			float playersBottom = playersTop + playersHeight;
			
			bool areSpectatorsPr = AreSpectatorsPresent();
			float spectatorsHeight = areSpectatorsPr ? 78.0F : 0.0F;

			// draw shadow
			img = renderer.RegisterImage("Gfx/Scoreboard/TopShadow.tga");
			size.y = 32.0F;
			renderer.SetColorAlphaPremultiplied(MakeVector4(0, 0, 0, 0.2F));
			renderer.DrawImage(img, AABB2(0, teamBarTop - size.y, sw, size.y));
			renderer.DrawImage(img, AABB2(0, playersBottom + spectatorsHeight + size.y, sw, -size.y));

			// draw team bar
			img = renderer.RegisterImage("Gfx/White.tga");
			renderer.SetColorAlphaPremultiplied(AdjustColor(GetTeamColor(0), 0.8F, 0.3F));
			renderer.DrawImage(img, AABB2(0, teamBarTop, sw * 0.5F, teamBarHeight));
			renderer.SetColorAlphaPremultiplied(AdjustColor(GetTeamColor(1), 0.8F, 0.3F));
			renderer.DrawImage(img, AABB2(sw * 0.5F, teamBarTop, sw * 0.5F, teamBarHeight));

			img = renderer.RegisterImage("Gfx/Scoreboard/Grunt.png");
			size.x = 120.0F;
			size.y = 60.0F;
			renderer.DrawImage(img, AABB2(contentsLeft, playersTop - size.y, size.x, size.y));
			renderer.DrawImage(img, AABB2(contentsRight, playersTop - size.y, -size.x, size.y));

			str = world->GetTeamName(0);
			pos.x = contentsLeft + 120.0F;
			pos.y = teamBarTop + 5.0F;
			font.Draw(str, pos + MakeVector2(1, 2), 1.0F, MakeVector4(0, 0, 0, 0.5));
			font.Draw(str, pos, 1.0F, white);

			str = world->GetTeamName(1);
			pos.x = contentsRight - 120.0F - font.Measure(str).x;
			pos.y = teamBarTop + 5.0F;
			font.Draw(str, pos + MakeVector2(1, 2), 1.0F, MakeVector4(0, 0, 0, 0.5));
			font.Draw(str, pos, 1.0F, white);

			// draw scores
			int capLimit;
			if (ctf)
				capLimit = ctf->GetCaptureLimit();
			else if (tc)
				capLimit = tc->GetNumTerritories();
			else
				capLimit = -1;

			if (capLimit != -1) {
				str = Format("{0}-{1}", GetTeamScore(0), capLimit);
				pos.x = sw * 0.5F - font.Measure(str).x - 15.0F;
				pos.y = teamBarTop + 5.0F;
				font.Draw(str, pos, 1.0F, MakeVector4(1, 1, 1, 0.5));

				str = Format("{0}-{1}", GetTeamScore(1), capLimit);
				pos.x = sw * 0.5F + 15.0F;
				pos.y = teamBarTop + 5.0F;
				font.Draw(str, pos, 1.0F, MakeVector4(1, 1, 1, 0.5));
			}

			// players background
			img = renderer.RegisterImage("Gfx/Scoreboard/PlayersBg.png");
			renderer.SetColorAlphaPremultiplied(MakeVector4(0, 0, 0, 1));
			renderer.DrawImage(img, AABB2(0, playersTop, sw, playersHeight + spectatorsHeight));

			// draw players
			DrawPlayers(0, contentsLeft, playersTop, (contentsRight - contentsLeft) * 0.5F, playersHeight);
			DrawPlayers(1, (sw - 8.0F) * 0.5F, playersTop, (contentsRight - contentsLeft) * 0.5F, playersHeight);
			if (areSpectatorsPr)
				DrawSpectators(playersBottom, sw * 0.5F);
		}

		struct ScoreboardEntry {
			int id;
			int score;
			std::string name;
			bool alive;
			bool operator<(const ScoreboardEntry& ent) const { return score > ent.score; }
		};

		extern int palette[32][3];

		void ScoreboardView::DrawPlayers(int team, float left, float top, float width, float height) {
			IFont& font = client->fontManager->GetGuiFont();
			float rowHeight = 24.0F;
			char buf[256];
			Vector2 size;
			
			int numPlayers = 0;
			std::vector<ScoreboardEntry> entries;
			for (size_t i = 0; i < world->GetNumPlayerSlots(); i++) {
				auto maybePlayer = world->GetPlayer(static_cast<unsigned int>(i));
				if (!maybePlayer)
					continue;
				Player& player = maybePlayer.value();
				if (player.GetTeamId() != team)
					continue;

				ScoreboardEntry ent;
				ent.name = player.GetName();
				ent.score = player.GetScore();
				ent.alive = player.IsAlive();
				ent.id = static_cast<int>(i);
				entries.push_back(ent);
				numPlayers++;
			}

			std::sort(entries.begin(), entries.end());

			int maxRows = (int)floorf(height / rowHeight);
			int cols = std::max(1, (numPlayers + maxRows - 1) / maxRows);
			maxRows = (numPlayers + cols - 1) / cols;

			int row = 0, col = 0;
			float colWidth = width / (float)cols;

			const int colorMode = cg_minimapPlayerColor;

			for (const auto& ent : entries) {
				float rowY = top + 6.0F + row * rowHeight;
				float colX = left + col * colWidth;

				// draw player id
				sprintf(buf, "#%d", ent.id); // FIXME: 1-base?
				size = font.Measure(buf);
				if (colorMode) {
					IntVector3 colorplayer = MakeIntVector3(palette[ent.id][0], palette[ent.id][1], palette[ent.id][2]);
					Vector4 colorplayerF = ModifyColor(colorplayer);
					font.Draw(buf, MakeVector2(colX + 35.0F - size.x, rowY), 1.0F, colorplayerF);
				} else {
					font.Draw(buf, MakeVector2(colX + 35.0F - size.x, rowY), 1.0F, white);
				}

				// draw player name
				Vector4 color = ent.alive ? white : MakeVector4(0.5, 0.5, 0.5, 1);
				if (stmp::make_optional(ent.id) == world->GetLocalPlayerIndex())
					color = GetTeamColor(team);
				font.Draw(ent.name, MakeVector2(colX + 40.0F, rowY), 1.0F, color);

				// draw player score
				sprintf(buf, "%d", ent.score);
				size = font.Measure(buf);
				font.Draw(buf, MakeVector2(colX + colWidth - 10.0F - size.x, rowY), 1.0F, white);

				// draw intel icon
				if (ctf && ctf->PlayerHasIntel(world->GetPlayer(ent.id).value())) {
					float pulse = std::max(0.5F, fabsf(sinf(world->GetTime() * 4.0F)));
					renderer.SetColorAlphaPremultiplied(white * pulse);
					renderer.DrawImage(intelIcon, AABB2(floorf(colX + colWidth - 30.0F - size.x),
					                                    rowY + 1.0F, 18.0F, 18.0F));
				}

				row++;
				if (row >= maxRows) {
					col++;
					row = 0;
				}
			}
		}

		void ScoreboardView::DrawSpectators(float top, float centerX) const {
			IFont& font = client->fontManager->GetGuiFont();
			char buf[256];
			
			static const float xPixelSpectatorOffset = 20.0F;
			float totalPixelWidth = 0;

			int numSpectators = 0;
			std::vector<ScoreboardEntry> entries;
			for (size_t i = 0; i < world->GetNumPlayerSlots(); i++) {
				auto maybePlayer = world->GetPlayer(static_cast<unsigned int>(i));
				if (!maybePlayer)
					continue;
				Player& player = maybePlayer.value();
				if (player.GetTeamId() != spectatorTeamId)
					continue;

				ScoreboardEntry ent;
				ent.name = player.GetName();
				ent.id = static_cast<int>(i);
				entries.push_back(ent);
				numSpectators++;

				// Measure total width in pixels so that we can center align all the spectators
				sprintf(buf, "#%d", ent.id);
				totalPixelWidth += font.Measure(buf).x + font.Measure(ent.name).x + xPixelSpectatorOffset;
			}

			if (numSpectators == 0)
				return;

			strcpy(buf, _TrN("Client", "Spectator{1}", "Spectators{1}", numSpectators, "").c_str());

			auto isSquareFont = spectatorFont == &client->fontManager->GetSquareDesignFont();
			auto sizeSpecString = spectatorFont->Measure(buf);

			Vector2 pos = MakeVector2(centerX - sizeSpecString.x / 2, top + (isSquareFont ? 0 : 10));
			spectatorFont->Draw(buf, pos + MakeVector2(1, 2), 1.0F, MakeVector4(0, 0, 0, 0.5));
			spectatorFont->Draw(buf, pos, 1.0F, spectatorTextColor);

			float yOffset = top + sizeSpecString.y;
			float halfTotalX = totalPixelWidth / 2;
			float currentXoffset = centerX - halfTotalX;

			for (const auto& ent : entries) {
				sprintf(buf, "#%d", ent.id);
				font.Draw(buf, MakeVector2(currentXoffset, yOffset), 1.0F, spectatorIdColor);

				auto sizeName = font.Measure(ent.name);
				auto sizeID = font.Measure(buf);
				font.Draw(ent.name, MakeVector2(currentXoffset + sizeID.x + 5.0F, yOffset), 1.0F, white);

				currentXoffset += sizeID.x + sizeName.x + xPixelSpectatorOffset;
			}
		}

		bool ScoreboardView::AreSpectatorsPresent() const {
			for (size_t i = 0; i < world->GetNumPlayerSlots(); i++) {
				auto p = world->GetPlayer(static_cast<unsigned int>(i));
				if (p && p.value().GetTeamId() == spectatorTeamId)
					return true;
			}

			return false;
		}
	} // namespace client
} // namespace spades