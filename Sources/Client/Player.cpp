/*
 Copyright (c) 2013 yvt,
 based on code of pysnip (c) Mathias Kaerlev 2011-2012.

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

#include "Player.h"

#include "GameMap.h"
#include "GameMapWrapper.h"
#include "Grenade.h"
#include "HitTestDebugger.h"
#include "IWorldListener.h"
#include "PhysicsConstants.h"
#include "Weapon.h"
#include "World.h"
#include <Core/Debug.h>
#include <Core/Exception.h>
#include <Core/Settings.h>

DEFINE_SPADES_SETTING(cg_orientationSmoothing, "1");

namespace spades {
	namespace client {

		Player::Player(World& w, int pId, WeaponType wType, int tId, Vector3 pos, IntVector3 col)
		    : world(w) {
			SPADES_MARK_FUNCTION();

			lastJump = false;
			lastClimbTime = -100;
			lastJumpTime = -100;
			tool = ToolWeapon;
			airborne = false;
			wade = false;
			position = pos;
			velocity = MakeVector3(0, 0, 0);
			orientation = MakeVector3(tId ? -1.0F : 1, 0, 0);
			orientationSmoothed = orientation;
			eye = MakeVector3(0, 0, 0);
			moveDistance = 0.0F;
			moveSteps = 0;

			playerId = pId;
			weapon.reset(Weapon::CreateWeapon(wType, *this, *w.GetGameProperties()));
			weaponType = wType;
			teamId = tId;
			weapon->Reset();
			color = col;

			health = 100;
			lastHealth = health;
			grenades = 3;
			blockStocks = 50;
			blockColor = MakeIntVector3(111, 111, 111);

			nextSpadeTime = 0.0F;
			nextDigTime = 0.0F;
			nextBlockTime = 0.0F;
			nextGrenadeTime = 0.0F;
			firstDig = false;

			cookingGrenade = false;
			grenadeTime = 0.0F;
			lastReloadingTime = 0.0F;

			blockCursorActive = false;
			blockCursorDragging = false;
			pendingPlaceBlock = false;
			pendingRestock = false;
			canPending = false;

			respawnTime = 0.0F;
		}

		Player::~Player() { SPADES_MARK_FUNCTION(); }

		bool Player::IsLocalPlayer() { return world.GetLocalPlayer() == this; }

		void Player::SetInput(PlayerInput newInput) {
			SPADES_MARK_FUNCTION();

			if (!IsAlive())
				return;

			if (newInput.crouch != input.crouch) {
				if (newInput.crouch) {
					if (!airborne || !IsLocalPlayer()) {
						position.z += 0.9F;
						eye.z += 0.9F;
					}
				} else {
					// Refuse the standing-up request if there's no room
					if (!TryUncrouch()) {
						if (IsLocalPlayer()) {
							newInput.crouch = true;
						} else { // ... unless the request is from the server.
							position.z -= 0.9F;
							eye.z -= 0.9F;
						}
					}
				}
			}

			input = newInput;
		}

		void Player::SetWeaponInput(WeaponInput newInput) {
			SPADES_MARK_FUNCTION();

			if (!IsAlive())
				return;

			auto* listener = world.GetListener();

			if (IsWalking() && !input.crouch && input.sprint) {
				newInput.primary = false;
				newInput.secondary = false;
			}

			if (tool == ToolSpade) {
				if (newInput.primary)
					newInput.secondary = false;
				if (newInput.secondary != weapInput.secondary) {
					if (newInput.secondary) {
						nextDigTime = world.GetTime() + GetToolSecondaryDelay();
						firstDig = true;
					}
				}
			} else if (tool == ToolGrenade) {
				if (world.GetTime() < nextGrenadeTime)
					newInput.primary = false;
				if (grenades <= 0 && IsLocalPlayer())
					newInput.primary = false;

				if (newInput.primary != weapInput.primary) {
					if (!newInput.primary) {
						ThrowGrenade();
					} else {
						cookingGrenade = true;
						grenadeTime = world.GetTime();

						if (listener)
							listener->PlayerPulledGrenadePin(*this);
					}
				}
			} else if (tool == ToolBlock && IsLocalPlayer()) {
				if (world.GetTime() < nextBlockTime) {
					newInput.primary = false;
					newInput.secondary = false;
				}

				if (newInput.primary)
					newInput.secondary = false;
				if (newInput.secondary != weapInput.secondary) {
					if (newInput.secondary) {
						if (blockCursorActive) {
							blockCursorDragging = true;
							blockCursorDragPos = blockCursorPos;
						} else {
							if (listener) // cannot build; invalid position.
								listener->LocalPlayerBuildError(BuildFailureReason::InvalidPosition);
						}
					} else {
						if (blockCursorDragging) {
							if (blockCursorActive) {
								int blocks = world.CubeLineCount(blockCursorDragPos, blockCursorPos);
								if (blocks <= blockStocks) {
									if (listener)
										listener->LocalPlayerCreatedLineBlock(blockCursorDragPos, blockCursorPos);
								} else {
									if (listener) // cannot build; insufficient blocks.
										listener->LocalPlayerBuildError(BuildFailureReason::InsufficientBlocks);
								}
								nextBlockTime = world.GetTime() + GetToolSecondaryDelay();
							} else {
								if (listener) // cannot build; invalid position.
									listener->LocalPlayerBuildError(BuildFailureReason::InvalidPosition);
							}
						}

						blockCursorActive = false;
						blockCursorDragging = false;
					}
				}

				if (newInput.primary != weapInput.primary || newInput.primary) {
					if (newInput.primary) {
						if (!weapInput.primary)
							lastSingleBlockBuildSeqDone = false;
						if (blockStocks > 0 && blockCursorActive) {
							if (listener)
								listener->LocalPlayerBlockAction(blockCursorPos, BlockActionCreate);

							lastSingleBlockBuildSeqDone = true;
							nextBlockTime = world.GetTime() + GetToolPrimaryDelay();
						} else if (blockStocks > 0 && airborne && canPending) {
							pendingPlaceBlock = true;
							pendingPlaceBlockPos = blockCursorPos;
						}

						blockCursorActive = false;
						blockCursorDragging = false;
					} else {
						if (listener && !lastSingleBlockBuildSeqDone) // cannot build; invalid position.
							listener->LocalPlayerBuildError(BuildFailureReason::InvalidPosition);
					}
				}
			} else if (tool == ToolBlock) {
				if (newInput.secondary != weapInput.secondary && !newInput.secondary) {
					if (world.GetTime() > nextBlockTime)
						nextBlockTime = world.GetTime() + GetToolPrimaryDelay();
				}
			} else if (tool == ToolWeapon) {
				weapon->SetShooting(newInput.primary);
			} else {
				SPAssert(false);
			}

			weapInput = newInput;
		}

		void Player::Reload() {
			SPADES_MARK_FUNCTION();

			if (!IsAlive())
				return; // dead man cannot reload

			weapon->Reload();
		}

		void Player::ReloadDone(int clip, int stock) {
			weapon->ReloadDone(clip, stock);
		}

		void Player::Refill(int hp, int grenades, int blocks) {
			SPADES_MARK_FUNCTION();

			lastHealth = localPlayerHealth;
			localPlayerHealth = hp;
			localPlayerGrenades = grenades;
			localPlayerBlocks = blocks;
			pendingRestock = true;
		}

		void Player::Restock() {
			SPADES_MARK_FUNCTION();

			if (!IsAlive())
				return; // dead man cannot restock

			Refill();
			weapon->Restock();

			if (world.GetListener())
				world.GetListener()->PlayerRestocked(*this);
		}

		void Player::SetTool(spades::client::Player::ToolType t) {
			SPADES_MARK_FUNCTION();

			if (t == tool)
				return;

			ToolType oldTool = tool;
			tool = t;
			if (oldTool == ToolWeapon)
				weapon->SetShooting(false);
			if (tool == ToolWeapon)
				weapon->SetShooting(weapInput.primary);

			cookingGrenade = false;
			if (IsLocalPlayer()) {
				blockCursorActive = false;
				blockCursorDragging = false;

				WeaponInput winp;
				SetWeaponInput(winp);
			}

			if (world.GetListener())
				world.GetListener()->PlayerChangedTool(*this);
		}

		void Player::SetPosition(const spades::Vector3& v) {
			SPADES_MARK_FUNCTION();

			position = eye = v;
		}

		void Player::SetVelocity(const spades::Vector3& v) {
			SPADES_MARK_FUNCTION();

			velocity = v;
		}

		void Player::SetOrientation(const spades::Vector3& v) {
			SPADES_MARK_FUNCTION();

			orientation = v;
		}

		void Player::Turn(float longitude, float latitude) {
			SPADES_MARK_FUNCTION();

			Vector3 o = GetFront();

			float yaw = atan2f(o.y, o.x);
			float pitch = -atan2f(o.z, o.GetLength2D());

			yaw += longitude;
			pitch += latitude;

			// Check pitch bounds
			if (pitch > DEG2RAD(89))
				pitch = DEG2RAD(89);
			if (pitch < -DEG2RAD(89))
				pitch = -DEG2RAD(89);

			o.x = cosf(pitch) * cosf(yaw);
			o.y = cosf(pitch) * sinf(yaw);
			o.z = -sinf(pitch);

			SetOrientation(o);
		}

		void Player::SetHP(int hp, HurtType type, spades::Vector3 p) {
			lastHealth = health;
			health = hp;
			if (world.GetListener())
				world.GetListener()->LocalPlayerHurt(type, p);
		}

		void Player::UpdateSmooth(float dt) {
			SPADES_MARK_FUNCTION();

			// Smooth the player orientation
			if (!IsLocalPlayer()) {
				orientationSmoothed = orientationSmoothed * powf(0.9F, dt * 60.0F) +
				                      orientation * powf(0.1F, dt * 60.0F);
				orientationSmoothed = orientationSmoothed.Normalize();
			}
		}

		void Player::Update(float dt) {
			SPADES_MARK_FUNCTION();

			auto* listener = world.GetListener();

			MovePlayer(dt);

			if (tool == ToolSpade) {
				if (weapInput.primary) {
					if (world.GetTime() > nextSpadeTime) {
						UseSpade(false);
						nextSpadeTime = world.GetTime() + GetToolPrimaryDelay();
					}
				} else if (weapInput.secondary) {
					if (world.GetTime() > nextDigTime) {
						UseSpade(true);
						nextDigTime = world.GetTime() + GetToolSecondaryDelay();
						firstDig = false;
					}
				}
			} else if (tool == ToolBlock && IsLocalPlayer()) {
				Vector3 muzzle = GetEye(), dir = GetFront();

				const Handle<GameMap>& map = world.GetMap();
				SPAssert(map);

				int maxDepth = map->GroundDepth();

				GameMap::RayCastResult mapResult;
				mapResult = map->CastRay2(muzzle, dir, 12);

				canPending = false;
				if (blockCursorDragging) {
					// check the starting point is not floating
					auto start = blockCursorDragPos;
					if (!map->IsSolidWrapped(start.x, start.y, start.z + 1) &&
					    !map->IsSolidWrapped(start.x, start.y, start.z - 1) &&
					    !map->IsSolidWrapped(start.x, start.y + 1, start.z) &&
					    !map->IsSolidWrapped(start.x, start.y - 1, start.z) &&
					    !map->IsSolidWrapped(start.x + 1, start.y, start.z) &&
					    !map->IsSolidWrapped(start.x - 1, start.y, start.z)) {
						if (listener) // cannot build; floating
							listener->LocalPlayerBuildError(BuildFailureReason::InvalidPosition);
						blockCursorDragging = false;
					}
				}

				if (mapResult.hit
					&& InBuildRange(mapResult.hitBlock + mapResult.normal)
					&& !OverlapsWithBlock(mapResult.hitBlock + mapResult.normal)
					&& map->IsValidMapCoord(mapResult.hitBlock.x, mapResult.hitBlock.y, mapResult.hitBlock.z)
					&& (mapResult.hitBlock + mapResult.normal).z >= 0
					&& (mapResult.hitBlock + mapResult.normal).z < maxDepth
					&& !pendingPlaceBlock) {
					// Building is possible, and there's no delayed block placement.
					blockCursorActive = true;
					blockCursorPos = mapResult.hitBlock + mapResult.normal;
				} else if (pendingPlaceBlock) {
					// Delayed Placement: When player attempts to place a block
					// while jumping and placing block is currently impossible
					// building will be delayed until it becomes possible
					// as long as player is airborne.
					if (airborne == false || blockStocks <= 0) {
						// player is no longer airborne, or doesn't have a block to place.
						pendingPlaceBlock = false;
						lastSingleBlockBuildSeqDone = true;
					} else if (InBuildRange(mapResult.hitBlock + mapResult.normal)
						&& !OverlapsWithBlock(pendingPlaceBlockPos)
						&& map->IsValidMapCoord(pendingPlaceBlockPos.x, pendingPlaceBlockPos.y, pendingPlaceBlockPos.z)
						&& pendingPlaceBlockPos.z < maxDepth) {
						// now building became possible.
						if (listener)
							listener->LocalPlayerBlockAction(pendingPlaceBlockPos, BlockActionCreate);

						pendingPlaceBlock = false;
						lastSingleBlockBuildSeqDone = true;
						nextBlockTime = world.GetTime() + GetToolPrimaryDelay();
					}
				} else {
					// Delayed Block Placement can be activated only
					// when the only reason making placement impossible
					// is that block to be placed overlaps with the player's hitbox.
					canPending = mapResult.hit
						&& InBuildRange(mapResult.hitBlock + mapResult.normal)
						&& map->IsValidMapCoord(mapResult.hitBlock.x, mapResult.hitBlock.y, mapResult.hitBlock.z)
						&& (mapResult.hitBlock + mapResult.normal).z >= 0
						&& (mapResult.hitBlock + mapResult.normal).z < maxDepth;
					blockCursorActive = false;

					int dist = 11;
					for (; (dist >= 1) && InBuildRange(mapResult.hitBlock + mapResult.normal); dist--)
						mapResult = map->CastRay2(muzzle, dir, dist);
					for (; (dist < 12) && InBuildRange(mapResult.hitBlock + mapResult.normal); dist++)
						mapResult = map->CastRay2(muzzle, dir, dist);
					blockCursorPos = mapResult.hitBlock + mapResult.normal;
				}
			} else if (tool == ToolBlock) {
				if (weapInput.primary && world.GetTime() > nextBlockTime)
					nextBlockTime = world.GetTime() + GetToolPrimaryDelay();
			} else if (tool == ToolGrenade) {
				if (GetGrenadeCookTime() >= 3.0F)
					ThrowGrenade();
			}

			if (weapon->FrameNext(dt))
				FireWeapon();

			if (weapon->GetReloadProgress() < 1.0F) {
				lastReloadingTime = world.GetTime();
			} else if (weapon->IsReloading()) {
				// for some reason a server didn't return
				// WeaponReload packet.
				if (world.GetTime() - lastReloadingTime > 5.0F)
					weapon->ForceReloadDone();
			}

			if (IsLocalPlayer() && pendingRestock) {
				lastHealth = health;
				health = localPlayerHealth;
				grenades = localPlayerGrenades;
				blockStocks = localPlayerBlocks;
				pendingRestock = false;
			}
		}

		bool Player::RayCastApprox(spades::Vector3 start, spades::Vector3 dir, float tolerance) {
			Vector3 diff = position - start;

			// Skip if out of range.
			float sqDist2D = diff.GetSquaredLength2D();
			if (sqDist2D > FOG_DISTANCE_SQ)
				return false;

			// |P-A| * cos(theta)
			float c = Vector3::Dot(diff, dir);

			// Looking away?
			if (c <= 0.0F)
				return false;

			// |P-A|^2
			float sqDist3D = sqDist2D + diff.z * diff.z;

			// |P-A| * sin(theta)
			float dist = sqDist3D - (c * c);

			return dist < (tolerance * tolerance);
		}

		enum class HitBodyPart { None, Head, Torso, Leg1, Leg2, Arms };

		void Player::FireWeapon() {
			SPADES_MARK_FUNCTION();

			auto* listener = world.GetListener();

			Vector3 dir = GetFront();
			Vector3 muzzle = GetEye() + (dir * 0.01F);

			// for hit-test debugging
			std::map<int, HitTestDebugger::PlayerHit> playerHits;
			std::vector<Vector3> bulletVectors;

			int pellets = weapon->GetPelletSize();
			float spread = weapon->GetSpread();
			if (!weapInput.secondary)
				spread *= 2;

			const Handle<GameMap>& map = world.GetMap();
			SPAssert(map);

			// pyspades takes destroying more than one block as a speed hack (shotgun does this)
			bool blockDestroyed = false;

			// The custom state data, optionally set by `BulletHitPlayer`'s implementation
			std::unique_ptr<IBulletHitScanState> stateCell;

			Vector3 pelletDir = dir;
			for (int i = 0; i < pellets; i++) {
				// AoS 0.75's way (pelletDir shouldn't be normalized!)
				pelletDir.x += (SampleRandomFloat() - SampleRandomFloat()) * spread;
				pelletDir.y += (SampleRandomFloat() - SampleRandomFloat()) * spread;
				pelletDir.z += (SampleRandomFloat() - SampleRandomFloat()) * spread;

				dir = pelletDir.Normalize();

				// first do map raycast
				GameMap::RayCastResult mapResult;
				mapResult = map->CastRay2(muzzle, dir, 256);

				bool nearPlayer = false;
				stmp::optional<Player&> hitPlayer;
				float hitPlayerDist2D = 0.0F; // disregarding Z coordinate
				float hitPlayerDist3D = 0.0F;
				HitBodyPart hitPart = HitBodyPart::None;
				hitTag_t hitFlag = hit_None;

				for (size_t i = 0; i < world.GetNumPlayerSlots(); i++) {
					// TODO: This is a repeated pattern, add something like
					// `World::GetExistingPlayerRange()` returning a range
					auto maybeOther = world.GetPlayer(static_cast<unsigned int>(i));
					if (maybeOther == this || !maybeOther)
						continue;

					Player& other = maybeOther.value();
					if (!other.IsAlive() || other.IsSpectator())
						continue; // filter deads/spectators
					if (other.RayCastApprox(muzzle, dir)) {
						nearPlayer = true;
					} else {
						continue; // quickly reject players unlikely to be hit
					}

					Vector3 hitPos;
					HitBoxes hb = other.GetHitBoxes();
					if (hb.head.RayCast(muzzle, dir, &hitPos)) {
						float const dist = (hitPos - muzzle).GetLength2D();
						if (!hitPlayer || dist < hitPlayerDist2D || hitPart == HitBodyPart::Arms) {
							if (hitPlayer != other) {
								hitPlayer = other;
								hitFlag = hit_None;
							}
							hitFlag |= hit_Head;

							hitPlayerDist2D = dist;
							hitPlayerDist3D = (hitPos - muzzle).GetLength();
							hitPart = HitBodyPart::Head;
						}
					}

					if (hb.torso.RayCast(muzzle, dir, &hitPos)) {
						float const dist = (hitPos - muzzle).GetLength2D();
						if (!hitPlayer || dist < hitPlayerDist2D || hitPart == HitBodyPart::Arms) {
							if (hitPlayer != other) {
								hitPlayer = other;
								hitFlag = hit_None;
							}
							hitFlag |= hit_Torso;

							hitPlayerDist2D = dist;
							hitPlayerDist3D = (hitPos - muzzle).GetLength();
							hitPart = HitBodyPart::Torso;
						}
					}

					for (int j = 0; j < 2; j++) {
						if (hb.limbs[j].RayCast(muzzle, dir, &hitPos)) {
							float const dist = (hitPos - muzzle).GetLength2D();
							if (!hitPlayer || dist < hitPlayerDist2D) {
								if (hitPlayer != other) {
									hitPlayer = other;
									hitFlag = hit_None;
								}
								hitFlag |= hit_Legs;

								hitPlayerDist2D = dist;
								hitPlayerDist3D = (hitPos - muzzle).GetLength();
								switch (j) {
									case 0: hitPart = HitBodyPart::Leg1; break;
									case 1: hitPart = HitBodyPart::Leg2; break;
								}
							}
						}
					}

					// check arms only if no head or torso hit detected
					if (hitPart == HitBodyPart::Head || hitPart == HitBodyPart::Torso)
						continue;

					if (hb.limbs[2].RayCast(muzzle, dir, &hitPos)) {
						float const dist = (hitPos - muzzle).GetLength2D();
						if (!hitPlayer || dist < hitPlayerDist2D) {
							if (hitPlayer != other) {
								hitPlayer = other;
								hitFlag = hit_None;
							}
							hitFlag |= hit_Arms;

							hitPlayerDist2D = dist;
							hitPlayerDist3D = (hitPos - muzzle).GetLength();
							hitPart = HitBodyPart::Arms;
						}
					}
				}

				Vector3 finalHitPos = muzzle + dir * 128.0F;
				float hitBlockDist2D = (mapResult.hitPos - muzzle).GetLength2D();

				if (mapResult.hit && hitBlockDist2D <= FOG_DISTANCE &&
				    (!hitPlayer || hitBlockDist2D < hitPlayerDist2D)) {
					finalHitPos = mapResult.hitPos;

					IntVector3 outBlockPos = mapResult.hitBlock;
					if (map->IsValidMapCoord(outBlockPos.x, outBlockPos.y, outBlockPos.z)) {
						SPAssert(map->IsSolid(outBlockPos.x, outBlockPos.y, outBlockPos.z));

						// damage block
						if (outBlockPos.z < map->GroundDepth()) {
							uint32_t col = map->GetColor(outBlockPos.x, outBlockPos.y, outBlockPos.z);
							int health = (col >> 24) - weapon->GetDamage(HitTypeBlock);
							if (health <= 0 && !blockDestroyed) {
								health = 0;
								blockDestroyed = true;
								if (listener && IsLocalPlayer()) // send destroy cmd for local
									listener->LocalPlayerBlockAction(outBlockPos, BlockActionTool);
							}

							if (map->IsSolid(outBlockPos.x, outBlockPos.y, outBlockPos.z)) {
								col = (col & 0xFFFFFF) | ((health & 0xFF) << 24);
								map->Set(outBlockPos.x, outBlockPos.y, outBlockPos.z, true, col);
								world.MarkBlockForRegeneration(outBlockPos);
							}
						}

						if (listener)
							listener->BulletHitBlock(finalHitPos, outBlockPos, mapResult.normal);
					}
				} else if (hitPlayer) {
					finalHitPos = muzzle + dir * hitPlayerDist3D;

					if (this->IsLocalPlayer())
						bulletVectors.push_back(finalHitPos);

					HitType hitType;
					if (hitFlag & hit_Head || hitFlag & hit_Torso) {
						if (hitFlag & hit_Head)
							hitType = HitTypeHead;
						if (hitFlag & hit_Torso)
							hitType = HitTypeTorso;
					} else if (hitFlag & hit_Arms) {
						hitType = HitTypeArms;
					} else {
						hitType = HitTypeLegs;
					}

					switch (hitPart) {
						case HitBodyPart::Head:
							playerHits[hitPlayer->playerId].numHeadHits++;
							break;
						case HitBodyPart::Torso:
							playerHits[hitPlayer->playerId].numTorsoHits++;
							break;
						case HitBodyPart::Leg1:
							playerHits[hitPlayer->playerId].numLimbHits[0]++;
							break;
						case HitBodyPart::Leg2:
							playerHits[hitPlayer->playerId].numLimbHits[1]++;
							break;
						case HitBodyPart::Arms:
							playerHits[hitPlayer->playerId].numLimbHits[2]++;
							break;
						case HitBodyPart::None: SPAssert(false); break;
					}

					if (listener)
						listener->BulletHitPlayer(*hitPlayer,
							hitType, finalHitPos, *this, stateCell);
				}

				// register near shots
				if (listener && nearPlayer && IsLocalPlayer())
					listener->BulletNearPlayer(*this);

				if (listener)
					listener->AddBulletTracer(*this, muzzle, finalHitPos);
			} // one pellet done

			if (this->IsLocalPlayer()) {
				// do hit test debugging
				auto* debugger = world.GetHitTestDebugger();
				if (debugger && !playerHits.empty())
					debugger->SaveImage(playerHits, bulletVectors);

				// do weapon recoil
				Vector2 rec = weapon->GetRecoil();

				// vanilla's horizontial recoil is driven by a triangular wave generator.
				int timer = world.GetTimeMS();
				rec.x *= ((timer % 1024) < 512)
						? (timer % 512) - 255.5F
						: 255.5F - (timer % 512);

				// double recoil if walking and not aiming
				if (IsWalking() && !weapInput.secondary)
					rec *= 2;

				// double recoil if airborne, halve if crouching and not midair
				if (airborne)
					rec *= 2;
				else if (input.crouch)
					rec /= 2;

				Turn(rec.x, rec.y);
			}
		}

		void Player::ThrowGrenade() {
			SPADES_MARK_FUNCTION();

			if (!cookingGrenade)
				return;

			auto* listener = world.GetListener();

			if (IsLocalPlayer()) {
				Vector3 const dir = GetFront();
				Vector3 const muzzle = GetEye() + (dir * 0.1F);
				Vector3 const vel = IsAlive()
					? (dir + GetVelocity())
					: Vector3(0, 0, 0);

				float const fuse = 3.0F - GetGrenadeCookTime();

				auto nade = stmp::make_unique<Grenade>(world, muzzle, vel, fuse);
				if (listener)
					listener->PlayerThrewGrenade(*this, *nade);
				world.AddGrenade(std::move(nade));
				grenades--;
			} else {
				// grenade packet will be sent by server
				if (listener)
					listener->PlayerThrewGrenade(*this, {});
			}

			cookingGrenade = false;
			nextGrenadeTime = world.GetTime() + GetToolPrimaryDelay();
		}

		void Player::UseSpade(bool dig) {
			SPADES_MARK_FUNCTION();

			auto* listener = world.GetListener();

			Vector3 muzzle = GetEye(), dir = GetFront();

			const Handle<GameMap>& map = world.GetMap();
			SPAssert(map);

			// first do map raycast
			GameMap::RayCastResult mapResult;
			mapResult = map->CastRay2(muzzle, dir, 32);

			stmp::optional<Player&> hitPlayer;

			if (!dig) {
				for (size_t i = 0; i < world.GetNumPlayerSlots(); i++) {
					auto maybeOther = world.GetPlayer(static_cast<unsigned int>(i));
					if (maybeOther == this || !maybeOther)
						continue;

					Player& other = maybeOther.value();
					if (!other.IsAlive() || other.IsSpectator())
						continue; // filter deads/spectators
					if ((other.GetEye() - muzzle).GetSquaredLength() >
					    (MELEE_DISTANCE * MELEE_DISTANCE))
						continue; // skip players outside attack range
					if (!other.RayCastApprox(muzzle, dir))
						continue; // quickly reject players unlikely to be hit

					hitPlayer = other;
					break;
				}
			}

			IntVector3 outBlockPos = mapResult.hitBlock;
			IntVector3 outBlockNormal = mapResult.normal;

			Vector3 blockF = MakeVector3(outBlockPos) + 0.5F;
			Vector3 shiftedPos = blockF + (MakeVector3(outBlockNormal) * 0.6F);
			float hitBlockDist = ((dig ? blockF : shiftedPos) - muzzle).GetChebyshevLength();

			if (mapResult.hit && hitBlockDist < MAX_DIG_DISTANCE && (!hitPlayer || dig)) {
				if (map->IsValidMapCoord(outBlockPos.x, outBlockPos.y, outBlockPos.z)) {
					SPAssert(map->IsSolid(outBlockPos.x, outBlockPos.y, outBlockPos.z));

					// damage block
					if (outBlockPos.z < map->GroundDepth()) {
						if (!dig) {
							uint32_t col = map->GetColor(outBlockPos.x, outBlockPos.y, outBlockPos.z);
							int health = (col >> 24) - 55;
							if (health <= 0) {
								health = 0;
								if (listener && IsLocalPlayer()) // send destroy cmd for local
									listener->LocalPlayerBlockAction(outBlockPos, BlockActionTool);
							}

							if (map->IsSolid(outBlockPos.x, outBlockPos.y, outBlockPos.z)) {
								col = (col & 0xFFFFFF) | ((health & 0xFF) << 24);
								map->Set(outBlockPos.x, outBlockPos.y, outBlockPos.z, true, col);
								world.MarkBlockForRegeneration(outBlockPos);
							}
						} else {
							if (listener && IsLocalPlayer())
								listener->LocalPlayerBlockAction(outBlockPos, BlockActionDig);
						}
					}

					if (listener)
						listener->PlayerHitBlockWithSpade(*this,
							mapResult.hitPos, outBlockPos, outBlockNormal);
				}
			} else if (hitPlayer && !dig) {
				// The custom state data, optionally set by `BulletHitPlayer`'s implementation
				std::unique_ptr<IBulletHitScanState> stateCell;

				if (listener)
					listener->BulletHitPlayer(*hitPlayer,
						HitTypeMelee, hitPlayer->GetEye(), *this, stateCell);
			}

			if (listener)
				listener->PlayerMissedSpade(*this);
		}

		Vector3 Player::GetFront(bool interpolate) {
			SPADES_MARK_FUNCTION_DEBUG();

			if (!IsLocalPlayer() && interpolate)
				return orientationSmoothed;

			return orientation;
		}

		Vector3 Player::GetFront2D() {
			SPADES_MARK_FUNCTION_DEBUG();
			return MakeVector3(orientation.x, orientation.y, 0).Normalize();
		}

		Vector3 Player::GetRight() {
			SPADES_MARK_FUNCTION_DEBUG();
			return -Vector3::Cross(MakeVector3(0, 0, -1), GetFront2D()).Normalize();
		}

		Vector3 Player::GetUp() {
			SPADES_MARK_FUNCTION_DEBUG();
			return Vector3::Cross(GetRight(), GetFront()).Normalize();
		}

		Vector3 Player::GetOrigin() {
			SPADES_MARK_FUNCTION_DEBUG();
			Vector3 v = eye;
			v.z += (input.crouch ? 0.45F : 0.9F);
			v.z += 0.3F;
			return v;
		}

		void Player::BoxClipMove(float fsynctics) {
			SPADES_MARK_FUNCTION();

			bool climb = false;
			float offset, m;
			if (input.crouch) {
				offset = 0.45F;
				m = 0.9F;
			} else {
				offset = 0.9F;
				m = 1.35F;
			}

			float f = fsynctics * 32.0F;
			float nx = f * velocity.x + position.x;
			float ny = f * velocity.y + position.y;
			float nz = position.z + offset;
			float z;

			const Handle<GameMap>& map = world.GetMap();
			SPAssert(map);

			z = m;
			float bx = nx + ((velocity.x < 0.0F) ? -0.45F : 0.45F);
			while (z >= -1.36F
				&& !map->ClipBox(bx, position.y - 0.45F, nz + z)
				&& !map->ClipBox(bx, position.y + 0.45F, nz + z))
				z -= 0.9F;
			if (z < -1.36F) {
				position.x = nx;
			} else if (!input.crouch && orientation.z < 0.5F && !input.sprint) {
				z = 0.35F;
				while (z >= -2.36F
					&& !map->ClipBox(bx, position.y - 0.45F, nz + z)
					&& !map->ClipBox(bx, position.y + 0.45F, nz + z))
					z -= 0.9F;
				if (z < -2.36F) {
					position.x = nx;
					climb = true;
				} else {
					velocity.x = 0.0F;
				}
			} else {
				velocity.x = 0.0F;
			}

			z = m;
			float by = ny + ((velocity.y < 0.0F) ? -0.45F : 0.45F);
			while (z >= -1.36F
				&& !map->ClipBox(position.x - 0.45F, by, nz + z)
				&& !map->ClipBox(position.x + 0.45F, by, nz + z))
				z -= 0.9F;
			if (z < -1.36F) {
				position.y = ny;
			} else if (!input.crouch && orientation.z < 0.5F && !input.sprint && !climb) {
				z = 0.35F;
				while (z >= -2.36F
					&& !map->ClipBox(position.x - 0.45F, by, nz + z)
					&& !map->ClipBox(position.x + 0.45F, by, nz + z))
					z -= 0.9F;
				if (z < -2.36F) {
					position.y = ny;
					climb = true;
				} else {
					velocity.y = 0.0F;
				}
			} else if (!climb) {
				velocity.y = 0.0F;
			}

			if (climb) {
				// slow down when climbing
				velocity.x *= 0.5F;
				velocity.y *= 0.5F;
				lastClimbTime = world.GetTime();
				nz--;
				m = -1.35F;
			} else {
				if (velocity.z < 0.0F)
					m = -m;
				nz += velocity.z * f;
			}

			airborne = true;
			if (map->ClipBox(position.x - 0.45F, position.y - 0.45F, nz + m) ||
			    map->ClipBox(position.x - 0.45F, position.y + 0.45F, nz + m) ||
			    map->ClipBox(position.x + 0.45F, position.y - 0.45F, nz + m) ||
			    map->ClipBox(position.x + 0.45F, position.y + 0.45F, nz + m)) {
				if (velocity.z >= 0.0F) {
					wade = position.z > 61.0F;
					airborne = false;
				}

				velocity.z = 0.0F;
			} else {
				position.z = nz - offset;
			}

			RepositionPlayer(position);
		}

		void Player::PlayerJump() {
			lastJump = true;
			velocity.z = -0.36F;

			if (world.GetListener() && world.GetTime() - lastJumpTime > 0.1F) {
				world.GetListener()->PlayerJumped(*this);
				lastJumpTime = world.GetTime();
			}
		}

		void Player::MoveCorpse(float fsynctics) {
			Vector3 oldPos = position; // old position

			// do velocity & gravity (friction is negligible)
			float f = fsynctics * 32.0F;
			velocity.z += fsynctics * 0.5F;
			position += velocity * f;

			const Handle<GameMap>& map = world.GetMap();
			SPAssert(map);

			// Collision
			IntVector3 lp = position.Floor();

			if (map->ClipWorld(lp.x, lp.y, lp.z)) {
				IntVector3 lp2 = oldPos.Floor();
				if (lp.z != lp2.z && ((lp.x == lp2.x && lp.y == lp2.y)
					|| !map->ClipWorld(lp.x, lp.y, lp2.z)))
					velocity.z = -velocity.z;
				else if (lp.x != lp2.x && ((lp.y == lp2.y && lp.z == lp2.z)
					|| !map->ClipWorld(lp2.x, lp.y, lp.z)))
					velocity.x = -velocity.x;
				else if (lp.y != lp2.y && ((lp.x == lp2.x && lp.z == lp2.z)
					|| !map->ClipWorld(lp.x, lp2.y, lp.z)))
					velocity.y = -velocity.y;

				position = oldPos; // set back to old position
				velocity *= 0.36F; // lose some velocity due to friction
			}

			if (map->ClipBox(position.x, position.y, position.z)) {
				velocity.z -= fsynctics * 6.0F;
				position += velocity * f;
			}

			SetPosition(position);
		}

		void Player::MovePlayer(float fsynctics) {
			if (!IsAlive()) {
				MoveCorpse(fsynctics);
				return;
			}

			if (input.jump && !lastJump && IsOnGroundOrWade()) {
				PlayerJump();
			} else if (!input.jump) {
				lastJump = false;
			}

			float f = fsynctics; // player acceleration scalar
			if (airborne)
				f *= 0.1F;
			else if (input.crouch)
				f *= 0.3F;
			else if (IsZoomed() || input.sneak)
				f *= 0.5F;
			else if (input.sprint)
				f *= 1.3F;

			if ((input.moveForward || input.moveBackward) && (input.moveRight || input.moveLeft))
				f *= sqrtf(0.5F); // if strafe + forward/backwards then limit diagonal velocity

			Vector3 front = GetFront();
			if (input.moveForward) {
				velocity.x += front.x * f;
				velocity.y += front.y * f;
			} else if (input.moveBackward) {
				velocity.x -= front.x * f;
				velocity.y -= front.y * f;
			}

			Vector3 right = GetRight();
			if (input.moveLeft) {
				velocity.x -= right.x * f;
				velocity.y -= right.y * f;
			} else if (input.moveRight) {
				velocity.x += right.x * f;
				velocity.y += right.y * f;
			}

			// this is a linear approximation that's done in pysnip
			// accurate computation is not difficult
			f = fsynctics + 1.0F;
			velocity.z += fsynctics;
			velocity.z /= f; // air friction

			if (wade) // water friction
				f = fsynctics * 6.0F + 1.0F;
			else if (!airborne) // ground friction
				f = fsynctics * 4.0F + 1.0F;

			velocity.x /= f;
			velocity.y /= f;

			float f2 = velocity.z;
			BoxClipMove(fsynctics);

			// hit ground... check if hurt
			if (!velocity.z && f2 > FALL_SLOW_DOWN) {
				// slow down on landing
				velocity.x *= 0.5F;
				velocity.y *= 0.5F;

				bool hurtOnLanding = f2 > FALL_DAMAGE_VELOCITY;
				if (world.GetListener())
					world.GetListener()->PlayerLanded(*this, hurtOnLanding);
			}

			float vel2D = velocity.GetSquaredLength2D();
			if (vel2D > 0.0F) {
				// count move distance, not based on velocity but on sprint state
				moveDistance += input.sprint ? 0.045F : 0.035F;

				bool madeFootstep = false;
				while (moveDistance > 1.0F) {
					moveSteps++;
					moveDistance -= 1.0F;

					if (world.GetListener() && !madeFootstep) {
						if (vel2D > 0.01F && IsOnGroundOrWade() &&
						    !input.crouch && !input.sneak && !IsZoomed())
							world.GetListener()->PlayerMadeFootstep(*this);
						madeFootstep = true;
					}
				}
			}
		}

		bool Player::TryUncrouch() {
			SPADES_MARK_FUNCTION();

			float x1 = position.x + 0.45F;
			float x2 = position.x - 0.45F;
			float y1 = position.y + 0.45F;
			float y2 = position.y - 0.45F;
			float z1 = position.z + 2.25F;
			float z2 = position.z - 1.35F;

			const Handle<GameMap>& map = world.GetMap();
			SPAssert(map);

			// first check if player can lower feet (in midair)
			if (airborne && !(map->ClipBox(x1, y1, z1)
				|| map->ClipBox(x2, y1, z1)
				|| map->ClipBox(x1, y2, z1)
				|| map->ClipBox(x2, y2, z1))) {
				if (!IsLocalPlayer()) {
					position.z -= 0.9F;
					eye.z -= 0.9F;
				}
				return true;
			// then check if they can raise their head
			} else if (!(map->ClipBox(x1, y1, z2)
				|| map->ClipBox(x2, y1, z2)
				|| map->ClipBox(x1, y2, z2)
				|| map->ClipBox(x2, y2, z2))) {
				position.z -= 0.9F;
				eye.z -= 0.9F;
				return true;
			}
			return false;
		}

		void Player::RepositionPlayer(const spades::Vector3& pos) {
			SPADES_MARK_FUNCTION();

			SetPosition(pos);
			float f = lastClimbTime - world.GetTime();
			float f2 = 0.25F;
			if (f > -f2)
				eye.z += (f + f2) / f2;
		}

		bool Player::IsReadyToUseTool() {
			SPADES_MARK_FUNCTION_DEBUG();
			switch (tool) {
				case ToolSpade: return true;
				case ToolBlock: return world.GetTime() > nextBlockTime && blockStocks > 0;
				case ToolGrenade: return world.GetTime() > nextGrenadeTime && grenades > 0;
				case ToolWeapon: return weapon->IsReadyToShoot();
				default: return false;
			}
		}

		bool Player::IsToolSelectable(ToolType type) {
			SPADES_MARK_FUNCTION_DEBUG();
			switch (type) {
				case ToolSpade: return true;
				case ToolBlock: return blockStocks > 0;
				case ToolGrenade: return grenades > 0;
				case ToolWeapon: return weapon->IsSelectable();
				default: return false;
			}
		}

		float Player::GetTimeToRespawn() { return respawnTime - world.GetTime(); }
		float Player::GetTimeToNextSpade() { return nextSpadeTime - world.GetTime(); }
		float Player::GetTimeToNextDig() { return nextDigTime - world.GetTime(); }
		float Player::GetTimeToNextBlock() { return nextBlockTime - world.GetTime(); }
		float Player::GetTimeToNextGrenade() { return nextGrenadeTime - world.GetTime(); }
		float Player::GetGrenadeCookTime() { return world.GetTime() - grenadeTime; }

		float Player::GetToolPrimaryDelay() {
			SPADES_MARK_FUNCTION_DEBUG();
			switch (tool) {
				case ToolSpade: return 0.2F;
				case ToolBlock:
				case ToolGrenade: return 0.5F;
				case ToolWeapon: return weapon->GetDelay();
				default: SPInvalidEnum("tool", tool);
			}
		}

		float Player::GetToolSecondaryDelay() {
			SPADES_MARK_FUNCTION_DEBUG();
			switch (tool) {
				case ToolSpade: return 1.0F;
				case ToolBlock: return GetToolPrimaryDelay();
				default: SPInvalidEnum("tool", tool);
			}
		}

		float Player::GetSpadeAnimationProgress() {
			SPADES_MARK_FUNCTION_DEBUG();
			SPAssert(tool == ToolSpade);
			SPAssert(weapInput.primary);
			return 1.0F - (GetTimeToNextSpade() / GetToolPrimaryDelay());
		}

		float Player::GetDigAnimationProgress() {
			SPADES_MARK_FUNCTION_DEBUG();
			SPAssert(tool == ToolSpade);
			SPAssert(weapInput.secondary);
			return 1.0F - (GetTimeToNextDig() / GetToolSecondaryDelay());
		}

		float Player::GetWalkAnimationProgress() {
			return moveDistance * 0.5F + (float)(moveSteps)*0.5F;
		}

		void Player::KilledBy(KillType type, Player& killer, int respawnTime) {
			SPADES_MARK_FUNCTION();

			health = 0;
			weapon->SetShooting(false);

			if (IsLocalPlayer() && tool == ToolBlock)
				blockCursorDragging = false; // do death cleanup

			// if local player is killed while cooking grenade, drop the live grenade.
			if (IsLocalPlayer() && tool == ToolGrenade)
				ThrowGrenade();

			if (world.GetListener())
				world.GetListener()->PlayerKilledPlayer(killer, *this, type);

			input = PlayerInput();
			weapInput = WeaponInput();
			this->respawnTime = world.GetTime() + respawnTime;
		}

		std::string Player::GetTeamName() { return world.GetTeamName(teamId); }
		std::string Player::GetName() { return world.GetPlayerName(playerId); }
		int Player::GetScore() { return world.GetPlayerScore(playerId); }

		IntVector3 Player::GetColor() {
			return IsSpectator() ? MakeIntVector3(200, 200, 200) : world.GetTeamColor(teamId);
		}

		Player::HitBoxes Player::GetHitBoxes() {
			SPADES_MARK_FUNCTION_DEBUG();

			Player::HitBoxes hb;

			Vector3 o = GetFront(cg_orientationSmoothing); // interpolated

			float yaw = atan2f(o.y, o.x) + M_PI_F * 0.5F;
			float pitch = -atan2f(o.z, o.GetLength2D());

			float armPitch = pitch;
			if (input.sprint)
				armPitch -= 0.9F;
			if (armPitch < 0.0F)
				armPitch = std::max(armPitch, -M_PI_F * 0.5F) * 0.9F;

			// lower axis
			Matrix4 const lower = Matrix4::Translate(GetOrigin())
				* Matrix4::Rotate(MakeVector3(0, 0, 1), yaw);
			Matrix4 const torso = lower
				* Matrix4::Translate(0, 0, -(input.crouch ? 0.5F : 1.0F));
			Matrix4 const head = torso
				* Matrix4::Translate(0.0F, 0.0F, -(input.crouch ? 0.05F : 0.0F))
				* Matrix4::Rotate(MakeVector3(1, 0, 0), pitch);
			Matrix4 const arms = torso
				* Matrix4::Translate(0, 0, input.crouch ? 0.0F : 0.1F)
				* Matrix4::Rotate(MakeVector3(1, 0, 0), armPitch);

			if (input.crouch) {
				hb.limbs[0] = lower * AABB3(-0.4F, -0.1F, -0.2F, 0.3F, 0.4F, 0.8F);
				hb.limbs[1] = lower * AABB3(0.1F, -0.1F, -0.2F, 0.3F, 0.4F, 0.8F);
				hb.torso = torso * AABB3(-0.4F, -0.1F, -0.1F, 0.8F, 0.8F, 0.7F);
				hb.limbs[2] = arms * AABB3(-0.6F, -1.0F, -0.1F, 1.2F, 1.0F, 0.6F);
				hb.head = head * AABB3(-0.3F, -0.3F, -0.6F, 0.6F, 0.6F, 0.6F);
			} else {
				hb.limbs[0] = lower * AABB3(-0.4F, -0.2F, -0.15F, 0.3F, 0.4F, 1.2F);
				hb.limbs[1] = lower * AABB3(0.1F, -0.2F, -0.15F, 0.3F, 0.4F, 1.2F);
				hb.torso = torso * AABB3(-0.4F, -0.2F, 0.0F, 0.8F, 0.4F, 0.9F);
				hb.limbs[2] = arms * AABB3(-0.6F, -1.0F, -0.1F, 1.2F, 1.0F, 0.6F);
				hb.head = head * AABB3(-0.3F, -0.3F, -0.6F, 0.6F, 0.6F, 0.6F);
			}

			return hb;
		}

		Weapon& Player::GetWeapon() {
			SPADES_MARK_FUNCTION();
			SPAssert(weapon);
			return *weapon;
		}

		void Player::SetWeaponType(WeaponType weap) {
			SPADES_MARK_FUNCTION_DEBUG();
			if (this->weapon->GetWeaponType() == weap)
				return;
			this->weapon.reset(Weapon::CreateWeapon(weap, *this, *world.GetGameProperties()));
			this->weaponType = weap;
		}

		bool Player::OverlapsWith(const spades::AABB3& box) {
			SPADES_MARK_FUNCTION_DEBUG();
			float offset, m;
			if (input.crouch) {
				offset = 0.45F;
				m = 0.9F;
			} else {
				offset = 0.9F;
				m = 1.35F;
			}
			m -= 0.5F;
			AABB3 playerBox(eye.x - 0.45F, eye.y - 0.45F, eye.z, 0.9F, 0.9F, offset + m);
			return box.Intersects(playerBox);
		}

		bool Player::OverlapsWithBlock(const spades::IntVector3& v) {
			SPADES_MARK_FUNCTION_DEBUG();
			Vector3 blockF = MakeVector3(v);
			return OverlapsWith(AABB3(blockF.x, blockF.y, blockF.z, 1, 1, 1));
		}

#pragma mark - Block Construction

		float Player::GetDistanceToBlock(const spades::IntVector3& v) {
			Vector3 blockF = MakeVector3(v) + 0.5F;
			return (blockF - GetEye()).GetChebyshevLength();
		}

		bool Player::InBuildRange(const spades::IntVector3& v) {
			return GetDistanceToBlock(v) < MAX_BLOCK_DISTANCE;
		}
	} // namespace client
} // namespace spades