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
#include <array>
#include <cmath>
#include <cstdlib>

#include "CTFGameMode.h"
#include "Client.h"
#include "ClientPlayer.h"
#include "GameMap.h"
#include "GunCasing.h"
#include "IAudioChunk.h"
#include "IAudioDevice.h"
#include "IImage.h"
#include "IModel.h"
#include "IRenderer.h"
#include "Player.h"
#include "Weapon.h"
#include "World.h"
#include "NetClient.h"
#include <Core/Bitmap.h>
#include <Core/Settings.h>
#include <ScriptBindings/IBlockSkin.h>
#include <ScriptBindings/IGrenadeSkin.h>
#include <ScriptBindings/ISpadeSkin.h>
#include <ScriptBindings/IThirdPersonToolSkin.h>
#include <ScriptBindings/IToolSkin.h>
#include <ScriptBindings/IViewToolSkin.h>
#include <ScriptBindings/IWeaponSkin.h>
#include <ScriptBindings/ScriptFunction.h>

#undef interface

SPADES_SETTING(cg_ragdoll);
DEFINE_SPADES_SETTING(cg_animations, "1");
SPADES_SETTING(cg_shake);
SPADES_SETTING(r_hdr);
DEFINE_SPADES_SETTING(cg_environmentalAudio, "1");
DEFINE_SPADES_SETTING(cg_classicPlayerModels, "0");
DEFINE_SPADES_SETTING(cg_classicViewWeapon, "0");
DEFINE_SPADES_SETTING(cg_viewWeaponX, "0");
DEFINE_SPADES_SETTING(cg_viewWeaponY, "0");
DEFINE_SPADES_SETTING(cg_viewWeaponZ, "0");
DEFINE_SPADES_SETTING(cg_hideBody, "0");
DEFINE_SPADES_SETTING(cg_hideArms, "0");
DEFINE_SPADES_SETTING(cg_debugToolSkinAnchors, "0");
DEFINE_SPADES_SETTING(cg_trueAimDownSight, "1");

SPADES_SETTING(cg_orientationSmoothing);

namespace spades {
	namespace client {

		class SandboxedRenderer : public IRenderer {
			Handle<IRenderer> base;
			AABB3 clipBox;
			bool allowDepthHack;

			void OnProhibitedAction() {}

			bool CheckVisibility(const AABB3& box) {
				if (!clipBox.Contains(box) ||
					!std::isfinite(box.min.x) ||
				    !std::isfinite(box.min.y) ||
					!std::isfinite(box.min.z) ||
				    !std::isfinite(box.max.x) ||
					!std::isfinite(box.max.y) ||
				    !std::isfinite(box.max.z)) {
					OnProhibitedAction();
					return false;
				}
				return true;
			}

		protected:
			~SandboxedRenderer() {}

		public:
			SandboxedRenderer(Handle<IRenderer> base) : base(std::move(base)) {}

			void SetClipBox(const AABB3& b) { clipBox = b; }
			void SetAllowDepthHack(bool h) { allowDepthHack = h; }

			void Init() { OnProhibitedAction(); }
			void Shutdown() { OnProhibitedAction(); }

			Handle<IImage> RegisterImage(const char* filename) {
				return base->RegisterImage(filename);
			}
			Handle<IModel> RegisterModel(const char* filename) {
				return base->RegisterModel(filename);
			}

			Handle<IImage> CreateImage(Bitmap& bmp) { return base->CreateImage(bmp); }
			Handle<IModel> CreateModel(VoxelModel& m) { return base->CreateModel(m); }

			void SetGameMap(stmp::optional<GameMap&>) { OnProhibitedAction(); }

			void SetFogDistance(float) { OnProhibitedAction(); }
			void SetFogColor(Vector3) { OnProhibitedAction(); }

			void StartScene(const SceneDefinition&) { OnProhibitedAction(); }

			void AddLight(const client::DynamicLightParam& light) {
				AABB3 aabb{light.origin, light.origin};
				if (light.type == DynamicLightTypeLinear)
					aabb += light.point2;

				if (CheckVisibility(aabb.Inflate(light.radius)))
					base->AddLight(light);
			}

			void RenderModel(IModel& model, const ModelRenderParam& _p) {
				ModelRenderParam p = _p;

				if (p.depthHack && !allowDepthHack) {
					OnProhibitedAction();
					return;
				}

				// Disable depth hack when `cg_debugToolSkinAnchors` is set
				// so that the drawn debug lines intersect with the weapon model
				if (cg_debugToolSkinAnchors)
					p.depthHack = false;

				// Overbright surfaces bypass the fog
				p.customColor.x = Clamp(p.customColor.x, 0.0F, 1.0F);
				p.customColor.y = Clamp(p.customColor.y, 0.0F, 1.0F);
				p.customColor.z = Clamp(p.customColor.z, 0.0F, 1.0F);

				// NaN values bypass the fog
				if (p.customColor.IsNaN()) {
					OnProhibitedAction();
					return;
				}

				auto bounds = (p.matrix * OBB3(model.GetBoundingBox())).GetBoundingAABB();
				if (CheckVisibility(bounds))
					base->RenderModel(model, p);
			}
			void AddDebugLine(Vector3 a, Vector3 b, Vector4 color) { OnProhibitedAction(); }

			void AddSprite(IImage& image, Vector3 center, float radius, float rotation) {
				Vector3 rad(radius * 1.5F, radius * 1.5F, radius * 1.5F);
				if (CheckVisibility(AABB3(center - rad, center + rad)))
					base->AddSprite(image, center, radius, rotation);
			}
			void AddLongSprite(IImage& image, Vector3 p1, Vector3 p2, float radius) {
				Vector3 rad(radius * 1.5F, radius * 1.5F, radius * 1.5F);
				AABB3 bounds1(p1 - rad, p1 + rad);
				AABB3 bounds2(p2 - rad, p2 + rad);
				bounds1 += bounds2;
				if (CheckVisibility(bounds1))
					base->AddLongSprite(image, p1, p2, radius);
			}

			void EndScene() { OnProhibitedAction(); }

			void MultiplyScreenColor(Vector3) { OnProhibitedAction(); }

			/** Sets color for image drawing. Deprecated because
			 * some methods treats this as an alpha premultiplied, while
			 * others treats this as an alpha non-premultiplied.
			 * @deprecated */
			void SetColor(Vector4 col) { base->SetColor(col); }

			/** Sets color for image drawing. Always alpha premultiplied. */
			void SetColorAlphaPremultiplied(Vector4 col) { base->SetColorAlphaPremultiplied(col); }

			void DrawImage(stmp::optional<IImage&> img, const Vector2& outTopLeft) {
				if (allowDepthHack)
					base->DrawImage(img, outTopLeft);
				else
					OnProhibitedAction();
			}
			void DrawImage(stmp::optional<IImage&> img, const AABB2& outRect) {
				if (allowDepthHack)
					base->DrawImage(img, outRect);
				else
					OnProhibitedAction();
			}
			void DrawImage(stmp::optional<IImage&> img,
				const Vector2& outTopLeft, const AABB2& inRect) {
				if (allowDepthHack)
					base->DrawImage(img, outTopLeft, inRect);
				else
					OnProhibitedAction();
			}
			void DrawImage(stmp::optional<IImage&> img,
				const AABB2& outRect, const AABB2& inRect) {
				if (allowDepthHack)
					base->DrawImage(img, outRect, inRect);
				else
					OnProhibitedAction();
			}
			void DrawImage(stmp::optional<IImage&> img, const Vector2& outTopLeft,
			               const Vector2& outTopRight, const Vector2& outBottomLeft,
			               const AABB2& inRect) {
				if (allowDepthHack)
					base->DrawImage(img, outTopLeft, outTopRight, outBottomLeft, inRect);
				else
					OnProhibitedAction();
			}

			void UpdateFlatGameMap() { OnProhibitedAction(); }
			void DrawFlatGameMap(const AABB2& outRect, const AABB2& inRect) {
				OnProhibitedAction();
			}

			void FrameDone() { OnProhibitedAction(); }
			void Flip() { OnProhibitedAction(); }

			Handle<Bitmap> ReadBitmap() {
				OnProhibitedAction();
				return {};
			}

			float ScreenWidth() { return base->ScreenWidth(); }
			float ScreenHeight() { return base->ScreenHeight(); }
		};

		ClientPlayer::ClientPlayer(Player& p, Client& c)
		    : client(c), player(p), hasValidOriginMatrix(false) {
			SPADES_MARK_FUNCTION();

			sprintState = 0.0F;
			aimDownState = 0.0F;
			toolRaiseState = 0.0F;
			currentTool = p.GetTool();
			time = 0.0F;
			viewWeaponOffset = MakeVector3(0, 0, 0);
			lastFront = MakeVector3(0, 0, 0);
			flashlightOrientation = p.GetFront();

			ScriptContextHandle ctx;
			IAudioDevice& audio = client.GetAudioDevice();

			sandboxedRenderer = Handle<SandboxedRenderer>::New(client.GetRenderer());
			IRenderer& renderer = *sandboxedRenderer;

			static ScriptFunction spadeFactory(
			  "ISpadeSkin@ CreateThirdPersonSpadeSkin(Renderer@, AudioDevice@)");
			spadeSkin = initScriptFactory(spadeFactory, renderer, audio);

			static ScriptFunction spadeViewFactory(
			  "ISpadeSkin@ CreateViewSpadeSkin(Renderer@, AudioDevice@)");
			spadeViewSkin = initScriptFactory(spadeViewFactory, renderer, audio);

			static ScriptFunction blockFactory(
			  "IBlockSkin@ CreateThirdPersonBlockSkin(Renderer@, AudioDevice@)");
			blockSkin = initScriptFactory(blockFactory, renderer, audio);

			static ScriptFunction blockViewFactory(
			  "IBlockSkin@ CreateViewBlockSkin(Renderer@, AudioDevice@)");
			blockViewSkin = initScriptFactory(blockViewFactory, renderer, audio);

			static ScriptFunction grenadeFactory(
			  "IGrenadeSkin@ CreateThirdPersonGrenadeSkin(Renderer@, AudioDevice@)");
			grenadeSkin = initScriptFactory(grenadeFactory, renderer, audio);

			static ScriptFunction grenadeViewFactory(
			  "IGrenadeSkin@ CreateViewGrenadeSkin(Renderer@, AudioDevice@)");
			grenadeViewSkin = initScriptFactory(grenadeViewFactory, renderer, audio);

			static ScriptFunction rifleFactory(
			  "IWeaponSkin@ CreateThirdPersonRifleSkin(Renderer@, AudioDevice@)");
			static ScriptFunction smgFactory(
			  "IWeaponSkin@ CreateThirdPersonSMGSkin(Renderer@, AudioDevice@)");
			static ScriptFunction shotgunFactory(
			  "IWeaponSkin@ CreateThirdPersonShotgunSkin(Renderer@, AudioDevice@)");
			static ScriptFunction rifleViewFactory(
			  "IWeaponSkin@ CreateViewRifleSkin(Renderer@, AudioDevice@)");
			static ScriptFunction smgViewFactory(
			  "IWeaponSkin@ CreateViewSMGSkin(Renderer@, AudioDevice@)");
			static ScriptFunction shotgunViewFactory(
			  "IWeaponSkin@ CreateViewShotgunSkin(Renderer@, AudioDevice@)");
			switch (p.GetWeapon().GetWeaponType()) {
				case RIFLE_WEAPON:
					weaponSkin = initScriptFactory(rifleFactory, renderer, audio);
					weaponViewSkin = initScriptFactory(rifleViewFactory, renderer, audio);
					break;
				case SMG_WEAPON:
					weaponSkin = initScriptFactory(smgFactory, renderer, audio);
					weaponViewSkin = initScriptFactory(smgViewFactory, renderer, audio);
					break;
				case SHOTGUN_WEAPON:
					weaponSkin = initScriptFactory(shotgunFactory, renderer, audio);
					weaponViewSkin = initScriptFactory(shotgunViewFactory, renderer, audio);
					break;
				default: SPAssert(false);
			}
		}
		ClientPlayer::~ClientPlayer() {
			spadeSkin->Release();
			blockSkin->Release();
			weaponSkin->Release();
			grenadeSkin->Release();

			spadeViewSkin->Release();
			blockViewSkin->Release();
			weaponViewSkin->Release();
			grenadeViewSkin->Release();
		}

		asIScriptObject* ClientPlayer::initScriptFactory(ScriptFunction& creator,
			IRenderer& renderer, IAudioDevice& audio) {
			ScriptContextHandle ctx = creator.Prepare();
			ctx->SetArgObject(0, reinterpret_cast<void*>(&renderer));
			ctx->SetArgObject(1, reinterpret_cast<void*>(&audio));
			ctx.ExecuteChecked();
			asIScriptObject* res = reinterpret_cast<asIScriptObject*>(ctx->GetReturnObject());
			res->AddRef();
			return res;
		}

		bool ClientPlayer::IsChangingTool() {
			return (currentTool != player.GetTool()) || (toolRaiseState < 0.999F);
		}

		void ClientPlayer::Update(float dt) {
			time += dt;

			bool isLocalPlayer = player.IsLocalPlayer();

			PlayerInput actualInput = player.GetInput();
			WeaponInput actualWeapInput = player.GetWeaponInput();

			if (player.IsToolWeapon() && actualWeapInput.secondary) {
				if (cg_animations && isLocalPlayer) {
					aimDownState += dt * 8.0F;
					if (aimDownState > 1.0F)
						aimDownState = 1.0F;
				} else {
					aimDownState = 1.0F;
				}
			} else {
				if (cg_animations && isLocalPlayer) {
					aimDownState -= dt * 3.0F;
					if (aimDownState < 0.0F)
						aimDownState = 0.0F;
				} else {
					aimDownState = 0.0F;
				}
			}

			if (actualInput.sprint) {
				sprintState += dt * 4.0F;
				if (sprintState > 1.0F)
					sprintState = 1.0F;
			} else {
				sprintState -= dt * 3.0F;
				if (sprintState < 0.0F)
					sprintState = 0.0F;
			}

			if (currentTool == player.GetTool()) {
				if (isLocalPlayer) {
					toolRaiseState += dt * 4.0F;
					if (toolRaiseState > 1.0F)
						toolRaiseState = 1.0F;
				} else {
					toolRaiseState = 1.0F;
				}
			} else {
				if (isLocalPlayer) {
					toolRaiseState -= dt * 4.0F;
					if (toolRaiseState < 0.0F) {
						toolRaiseState = 0.0F;
						currentTool = player.GetTool();
						client.net->SendTool();

						// play tool change sound
						IAudioDevice& audioDevice = client.GetAudioDevice();
						Handle<IAudioChunk> c;
						switch (currentTool) {
							case Player::ToolSpade:
								c = audioDevice.RegisterSound("Sounds/Weapons/Spade/RaiseLocal.opus");
								break;
							case Player::ToolBlock:
								c = audioDevice.RegisterSound("Sounds/Weapons/Block/RaiseLocal.opus");
								break;
							case Player::ToolWeapon:
								switch (player.GetWeapon().GetWeaponType()) {
									case RIFLE_WEAPON:
										c = audioDevice.RegisterSound(
										  "Sounds/Weapons/Rifle/RaiseLocal.opus");
										break;
									case SMG_WEAPON:
										c = audioDevice.RegisterSound(
										  "Sounds/Weapons/SMG/RaiseLocal.opus");
										break;
									case SHOTGUN_WEAPON:
										c = audioDevice.RegisterSound(
										  "Sounds/Weapons/Shotgun/RaiseLocal.opus");
										break;
								}
								break;
							case Player::ToolGrenade:
								c = audioDevice.RegisterSound(
								  "Sounds/Weapons/Grenade/RaiseLocal.opus");
								break;
						}
						audioDevice.PlayLocal(c.GetPointerOrNull(),
							MakeVector3(0.4F, -0.3F, 0.5F), AudioParam());
					}
				} else {
					toolRaiseState = 0.0F;
					currentTool = player.GetTool();
				}
			}

			bool isThirdPerson = ShouldRenderInThirdPersonView();
			if (!isThirdPerson) {
				Vector3 front = player.GetFront();
				Vector3 right = player.GetRight();
				Vector3 up = player.GetUp();

				if (cg_classicViewWeapon) {
					// Offset the view weapon according to the camera movement
					Vector3 diff = front - lastFront;
					viewWeaponOffset.x += Vector3::Dot(diff, right);
					viewWeaponOffset.z += Vector3::Dot(diff, up);
					lastFront = front;

					if (dt > 0.0F)
						viewWeaponOffset *= powf(1.0E-6F, dt);
				} else {
					float scale = dt;

					// Offset the view weapon according to the player movement
					Vector3 v = player.GetVelocity();
					viewWeaponOffset.x += Vector3::Dot(v, right) * scale;
					viewWeaponOffset.y -= Vector3::Dot(v, front) * scale;
					viewWeaponOffset.z += Vector3::Dot(v, up) * scale;

					// Offset the view weapon according to the camera movement
					Vector3 diff = front - lastFront;
					viewWeaponOffset.x += Vector3::Dot(diff, right) * 0.05F;
					viewWeaponOffset.z += Vector3::Dot(diff, up) * 0.05F;
					lastFront = front;

					if (dt > 0.0F)
						viewWeaponOffset *= powf(0.02F, dt);

					// Limit the movement
					auto softLimitFunc = [&](float& v, float minLimit, float maxLimit) {
						float transition = (maxLimit - minLimit) * 0.5F;
						if (v < minLimit)
							v = Mix(v, minLimit, std::min(1.0F, (minLimit - v) / transition));
						if (v > maxLimit)
							v = Mix(v, maxLimit, std::min(1.0F, (v - maxLimit) / transition));
					};

					const float limitX = 0.006F;
					const float limitY = 0.006F;
					softLimitFunc(viewWeaponOffset.x, -limitX, limitY);
					softLimitFunc(viewWeaponOffset.y, -limitX, limitY);
					softLimitFunc(viewWeaponOffset.z, -limitX, limitY);

					// When the player is aiming down the sight, the weapon's movement
					// must be restricted so that other parts of the weapon don't
					// cover the ironsight.
					if (currentTool == Player::ToolWeapon && actualWeapInput.secondary) {
						if (dt > 0.0F)
							viewWeaponOffset *= powf(0.01F, dt);

						const float limitX = 0.003F;
						const float limitY = 0.003F;
						softLimitFunc(viewWeaponOffset.x, -limitX, limitX);
						softLimitFunc(viewWeaponOffset.z, 0, limitY);
					}
				}

				// Smooth the flashlight's movement
				if (client.flashlightOn && isLocalPlayer) {
					Vector3 diff = front - flashlightOrientation;
					float dist = diff.GetLength();
					if (dist > 0.1F)
						flashlightOrientation += diff.Normalize() * (dist - 0.1F);
					flashlightOrientation = Mix(flashlightOrientation, front, 1.0F - powf(1.0E-6F, dt));
					flashlightOrientation = flashlightOrientation.Normalize();
				}
			}

			// FIXME: should do for non-active skins?
			asIScriptObject* curSkin = GetCurrentSkin(!isThirdPerson);
			{
				ScriptIToolSkin interface(curSkin);
				interface.Update(dt);
			}
		}

		Matrix4 ClientPlayer::GetEyeMatrix() {
			Vector3 eye = player.GetEye();

			if ((int)cg_shake >= 2) {
				float p = cosf(player.GetWalkAnimationProgress() * M_PI_F * 2.0F - 0.8F);
				p = p * p;
				p *= p;
				p *= p;
				p *= p;
				eye.z -= p * 0.06F * SmoothStep(sprintState);
			}

			return Matrix4::FromAxis(-player.GetRight(), player.GetFront(), -player.GetUp(), eye);
		}

		void ClientPlayer::SetSkinParameterForTool(Player::ToolType type, asIScriptObject* skin) {
			Player& p = player;

			WeaponInput actualWeapInput = p.GetWeaponInput();

			const float primaryDelay = p.GetToolPrimaryDelay();
			const float secondaryDelay = p.GetToolSecondaryDelay();

			if (type == Player::ToolSpade) {
				ScriptISpadeSkin interface(skin);
				const float nextSpadeTime = p.GetTimeToNextSpade();
				if (nextSpadeTime > 0.0F) {
					interface.SetActionType(SpadeActionTypeBash);
					interface.SetActionProgress(1.0F - (nextSpadeTime / primaryDelay));
				} else if (actualWeapInput.secondary) {
					interface.SetActionType(p.IsFirstDig()
						? SpadeActionTypeDigStart : SpadeActionTypeDig);
					interface.SetActionProgress(1.0F - (p.GetTimeToNextDig() / secondaryDelay));
				} else {
					interface.SetActionType(SpadeActionTypeIdle);
					interface.SetActionProgress(0.0F);
				}
			} else if (type == Player::ToolBlock) {
				ScriptIBlockSkin interface(skin);
				interface.SetReadyState(1.0F - (p.GetTimeToNextBlock() / primaryDelay));
				interface.SetBlockColor(ConvertColorRGB(p.GetBlockColor()));
			} else if (type == Player::ToolGrenade) {
				ScriptIGrenadeSkin interface(skin);
				interface.SetReadyState(1.0F - (p.GetTimeToNextGrenade() / primaryDelay));
				interface.SetCookTime(actualWeapInput.primary ? p.GetGrenadeCookTime() : 0.0F);
			} else if (type == Player::ToolWeapon) {
				Weapon& w = p.GetWeapon();
				ScriptIWeaponSkin interface(skin);
				interface.SetReadyState(1.0F - (w.GetTimeToNextFire() / primaryDelay));
				interface.SetAimDownSightState(cg_trueAimDownSight ? aimDownState : aimDownState * 0.5F);
				interface.SetAmmo(w.GetAmmo());
				interface.SetClipSize(w.GetClipSize());
				interface.SetReloading(w.IsReloading());
				interface.SetReloadProgress(w.GetReloadProgress());
			} else {
				SPInvalidEnum("currentTool", type);
			}
		}

		asIScriptObject* ClientPlayer::GetCurrentSkin(bool viewSkin) {
			switch (currentTool) {
				case Player::ToolSpade: return viewSkin ? spadeViewSkin : spadeSkin; break;
				case Player::ToolBlock: return viewSkin ? blockViewSkin : blockSkin; break;
				case Player::ToolWeapon: return viewSkin ? weaponViewSkin : weaponSkin; break;
				case Player::ToolGrenade: return viewSkin ? grenadeViewSkin : grenadeSkin; break;
				default: SPInvalidEnum("currentTool", currentTool);
			}
		}

		void ClientPlayer::SetCommonSkinParameter(asIScriptObject* skin) {
			asIScriptObject* curSkin = GetCurrentSkin(!ShouldRenderInThirdPersonView());

			float sprint = SmoothStep(sprintState);
			float putdown = 1.0F - toolRaiseState;
			putdown *= putdown;
			putdown = std::min(1.0F, putdown * 1.5F);
			float raiseState = (skin == curSkin) ? (1.0F - putdown) : 0.0F;

			{
				ScriptIToolSkin interface(skin);
				interface.SetTeamColor(ConvertColorRGB(player.GetColor()));
				interface.SetRaiseState(player.IsLocalPlayer() ? raiseState : 1.0F);
				interface.SetSprintState(sprint);
				interface.SetMuted(client.IsMuted());
			}
		}

		std::array<Vector3, 3> ClientPlayer::GetFlashlightAxes() {
			std::array<Vector3, 3> axes;
			axes[2] = flashlightOrientation;
			axes[0] = Vector3::Cross(axes[2], player.GetUp()).Normalize();
			axes[1] = Vector3::Cross(axes[0], axes[2]);
			return axes;
		}

		void ClientPlayer::AddToSceneFirstPersonView() {
			Player& p = player;
			Weapon& w = p.GetWeapon();
			IRenderer& renderer = client.GetRenderer();
			World* world = client.GetWorld();
			Matrix4 eyeMatrix = GetEyeMatrix();
			Vector3 vel = p.GetVelocity();

			std::string modelPath = "Models/Player/";
			if (!cg_classicPlayerModels)
				modelPath += w.GetName() + "/";

			// Configure the clipping region for the localplayer view in case of overdraw
			{
				Vector3 const origin = eyeMatrix.GetOrigin();
				Vector3 const outset(20.0F, 20.0F, 20.0F);

				sandboxedRenderer->SetClipBox(AABB3(origin - outset, origin + outset));
				sandboxedRenderer->SetAllowDepthHack(true);
			}

			// no flashlight if spectating other players while dead
			if (client.flashlightOn && p.IsLocalPlayer()) {
				float brightness = client.time - client.flashlightOnTime;
				brightness = 1.0F - expf(-brightness * 5.0F);
				brightness *= r_hdr ? 3.0F : 1.5F;

				// add flash light
				DynamicLightParam light;
				Handle<IImage> img = renderer.RegisterImage("Gfx/Spotlight.jpg");
				light.origin = (eyeMatrix * MakeVector3(0, 0.3F, -0.3F)).GetXYZ();
				light.color = MakeVector3(1.0F, 0.7F, 0.5F) * brightness;
				light.radius = 60.0F;
				light.type = DynamicLightTypeSpotlight;
				light.spotAngle = DEG2RAD(90);
				light.spotAxis = GetFlashlightAxes();
				light.image = img.GetPointerOrNull();
				renderer.AddLight(light);

				light.color *= 0.3F;
				light.radius = 10.0F;
				light.type = DynamicLightTypePoint;
				light.image = nullptr;
				renderer.AddLight(light);
			}

			Vector3 leftHand, rightHand;
			leftHand = MakeVector3(0, 0, 0);
			rightHand = MakeVector3(0, 0, 0);

			// view weapon
			Vector3 viewWeaponOffset = this->viewWeaponOffset;

			Handle<IModel> model;
			ModelRenderParam param;
			param.depthHack = true;
			param.customColor = ConvertColorRGB(p.GetColor());

			// Moving this to the scripting environment means
			// breaking compatibility with existing scripts.
			if (cg_classicViewWeapon) {
				Matrix4 mat = Matrix4::Scale(0.033F);
				Vector3 trans(0.0F, 0.0F, 0.0F);

				float bob = std::max(fabsf(vel.x), fabsf(vel.y)) / 1000;
				int timer = (int)(time * 1000);
				bob *= (timer % 1024 < 512)
					? (timer % 512) - 255.5F
					: 255.5F - (timer % 512);
				trans.y += bob;

				if (!p.IsOnGroundOrWade())
					trans.z -= vel.z * 0.2F;

				float raiseState = p.IsLocalPlayer() ? toolRaiseState : 1.0F;
				if (sprintState > 0.0F || raiseState < 1.0F) {
					float per = std::max(sprintState, 1.0F - raiseState) * 5;
					trans.x -= per;
					trans.z += per;
				}

				WeaponInput actualWeapInput = p.GetWeaponInput();

				const float nextSpadeTime = p.GetTimeToNextSpade();
				const float nextDigTime = p.GetTimeToNextDig();
				const float nextBlockTime = p.GetTimeToNextBlock();
				const float nextFireTime = w.GetTimeToNextFire();

				const float primaryDelay = p.GetToolPrimaryDelay();
				const float secondaryDelay = p.GetToolSecondaryDelay();

				const float cookGrenadeTime = p.GetGrenadeCookTime();
				const float reloadProgress = 1.0F - w.GetReloadProgress();

				switch (currentTool) {
					case Player::ToolSpade:
						model = renderer.RegisterModel("Models/Weapons/Spade/Spade.kv6");
						if (nextSpadeTime > 0.0F) {
							float f = nextSpadeTime / primaryDelay;
							mat = Matrix4::Rotate(MakeVector3(1, 0, 0), f * 1.25F) * mat;
							mat = Matrix4::Translate(0.0F, f * 0.5F, f * 0.25F) * mat;
						} else if (actualWeapInput.secondary && nextDigTime > 0.0F) {
							float f = nextDigTime / secondaryDelay;
							float f2;
							if (f >= 0.6F) {
								f2 = 0.0F;
								f = 1.0F - f;
							} else if (f >= 0.3F) {
								f2 = 0.6F - f;
								f = 0.4F;
							} else if (f >= 0.1F) {
								f2 = 0.3F;
								f = 0.4F;
							} else {
								f2 = f * 3;
								f *= 4;
							}

							mat = Matrix4::Translate(MakeVector3(f2 * 0.5F, f * 0.25F, -f2 * 1.25F)) * mat;
							mat = Matrix4::Rotate(MakeVector3(1, 0, 0), f / 0.32F) * mat;
							mat = Matrix4::Rotate(MakeVector3(0, -1, 0), f) * mat;
						}
						break;
					case Player::ToolBlock:
						param.customColor = ConvertColorRGB(p.GetBlockColor());
						model = renderer.RegisterModel("Models/Weapons/Block/Block.kv6");
						if (nextBlockTime > 0.0F) {
							float f = nextBlockTime * 8;
							trans.x -= f;
							trans.z += f;
						}
						break;
					case Player::ToolGrenade:
						model = renderer.RegisterModel("Models/Weapons/Grenade/Grenade.kv6");
						if (p.IsCookingGrenade() && cookGrenadeTime > 0.0F) {
							trans.x -= cookGrenadeTime;
							trans.z -= cookGrenadeTime;
						}
						break;
					case Player::ToolWeapon: {
						// don't draw model when aiming
						if (aimDownState > 0.99F)
							return;

						switch (w.GetWeaponType()) {
							case RIFLE_WEAPON:
								model = renderer.RegisterModel("Models/Weapons/Rifle/Weapon.kv6");
								break;
							case SMG_WEAPON:
								model = renderer.RegisterModel("Models/Weapons/SMG/Weapon.kv6");
								break;
							case SHOTGUN_WEAPON:
								model = renderer.RegisterModel("Models/Weapons/Shotgun/Weapon.kv6");
								break;
						}

						if (reloadProgress > 0.0F && !w.IsReloadSlow()) {
							float f = reloadProgress * 8;
							trans.x -= f;
							trans.z += f;
						}

						if (nextFireTime > 0.0F) {
							float f = nextFireTime / 32;
							float f2 = f * 10;
							trans.x -= f2;
							trans.z += f2;
							mat = Matrix4::Rotate(MakeVector3(-1, 0, 0), DEG2RAD(f * 280)) * mat;
						}
					} break;
				}

				trans += Vector3(-0.33F, 0.66F, 0.4F);
				trans += 0.015F; // adjust to match voxlap
				trans += viewWeaponOffset;

				param.matrix = Matrix4::Translate(trans) * mat;
				param.matrix = eyeMatrix * param.matrix;
				renderer.RenderModel(*model, param);

				return;
			}

			// bobbing
			{
				float sp = 1.0F - aimDownState;
				sp *= 0.3F;
				sp *= std::min(1.0F, vel.GetLength() * 5.0F);

				float walkAng = p.GetWalkAnimationProgress() * M_PI_F * 2.0F;
				float vl = cosf(walkAng);
				vl *= vl;

				viewWeaponOffset.x += sinf(walkAng) * 0.013F * sp;
				viewWeaponOffset.z += vl * 0.018F * sp;
			}

			// slow pulse
			{
				float sp = 1.0F - aimDownState;
				float vl = sinf(time * 1.0F);

				viewWeaponOffset.x += vl * 0.001F * sp;
				viewWeaponOffset.y += vl * 0.0007F * sp;
				viewWeaponOffset.z += vl * 0.003F * sp;
			}

			// manual adjustment
			{
				float sp = 1.0F - aimDownState;

				viewWeaponOffset.x += (float)cg_viewWeaponX * sp;
				viewWeaponOffset.y += (float)cg_viewWeaponY * sp;
				viewWeaponOffset.z += (float)cg_viewWeaponZ * sp;
			}

			asIScriptObject* curSkin = GetCurrentSkin(true);
			SetSkinParameterForTool(currentTool, curSkin);
			SetCommonSkinParameter(curSkin);

			// common process
			{
				ScriptIViewToolSkin interface(curSkin);
				interface.SetEyeMatrix(eyeMatrix);
				interface.SetSwing(viewWeaponOffset);
			}
			{
				ScriptIToolSkin interface(curSkin);
				interface.AddToScene();
			} 
			{
				ScriptIViewToolSkin interface(curSkin);
				leftHand = interface.GetLeftHandPosition();
				rightHand = interface.GetRightHandPosition();
			}

			Vector3 o = p.GetFront();

			float yaw = atan2f(o.y, o.x) + M_PI_F * 0.5F;

			// lower axis
			Matrix4 const lower = Matrix4::Translate(p.GetOrigin())
				* Matrix4::Rotate(MakeVector3(0, 0, 1), yaw);

			Matrix4 const scaler = Matrix4::Scale(0.1F)
				* Matrix4::Scale(-1, -1, 1);

			PlayerInput const inp = p.GetInput();

			float const legsPosY = inp.crouch ? 1.25F : 1.0F;
			float const legsPosZ = inp.crouch ? 0.05F : 0.1F;
			float const torsoPosZ = inp.crouch ? 0.55F : 1.0F;

			Vector2 legsRot;
			legsRot.x = Vector3::Dot(vel, p.GetFront2D());
			legsRot.y = Vector3::Dot(vel, p.GetRight());
			legsRot *= sinf(p.GetWalkAnimationProgress() * M_PI_F * 2.0F) * 3.0F;

			Matrix4 const leg1 = lower
				* Matrix4::Translate(0.25F, legsPosY, -legsPosZ)
				* Matrix4::Rotate(MakeVector3(1, 0, 0), legsRot.x)
				* Matrix4::Rotate(MakeVector3(0, 1, 0), legsRot.y);

			Matrix4 const leg2 = lower
				* Matrix4::Translate(-0.25F, legsPosY, -legsPosZ)
				* Matrix4::Rotate(MakeVector3(1, 0, 0), -legsRot.x)
				* Matrix4::Rotate(MakeVector3(0, 1, 0), -legsRot.y);

			Matrix4 const torso = lower
				* Matrix4::Translate(0.0F, 1.0F, -torsoPosZ);

			// Legs and Torso
			if (!cg_hideBody) {
				model = inp.crouch
					? renderer.RegisterModel((modelPath + "LegCrouch.kv6").c_str())
					: renderer.RegisterModel((modelPath + "Leg.kv6").c_str());
				param.matrix = leg1 * scaler;
				renderer.RenderModel(*model, param);
				param.matrix = leg2 * scaler;
				renderer.RenderModel(*model, param);

				model = inp.crouch
					? renderer.RegisterModel((modelPath + "TorsoCrouch.kv6").c_str())
					: renderer.RegisterModel((modelPath + "Torso.kv6").c_str());
				param.matrix = torso * scaler;
				renderer.RenderModel(*model, param);
			}

			// Arms
			if (!cg_hideArms && leftHand.GetSquaredLength() > 0.01F && rightHand.GetSquaredLength() > 0.01F) {
				Handle<IModel> armModel = renderer.RegisterModel((modelPath + "Arm.kv6").c_str());
				Handle<IModel> upperModel = renderer.RegisterModel((modelPath + "UpperArm.kv6").c_str());

				const float armlen = 0.5F;
				const Matrix4 armsScale = Matrix4::Scale(0.05F);

				Vector3 shoulders[] = {{0.4F, 0.0F, 0.25F}, {-0.4F, 0.0F, 0.25F}};
				Vector3 hands[] = {leftHand, rightHand};
				Vector3 benddirs[] = {{0.5F, 0.2F, 0.0F}, {-0.5F, 0.2F, 0.0F}};

				auto addModel = [&](IModel& model, Vector3 v1, Vector3 v2) {
					Vector3 axises[3];
					axises[2] = (v1 - v2).Normalize();
					axises[1] = Vector3::Cross(axises[2], MakeVector3(0, 0, 1)).Normalize();
					axises[0] = Vector3::Cross(axises[1], axises[2]).Normalize();

					param.matrix = Matrix4::FromAxis(axises[0], axises[1], axises[2], v2);
					param.matrix = eyeMatrix * param.matrix * armsScale;
					renderer.RenderModel(model, param);
				};

				for (int i = 0; i < 2; i++) {
					Vector3 shoulder = shoulders[i] + viewWeaponOffset;
					Vector3 hand = hands[i];
					Vector3 benddir = benddirs[i];

					Vector3 bend = Vector3::Cross(benddir, hand - shoulder).Normalize();
					if (bend.z < 0.0F)
						bend.z = -bend.z;

					float const distSqr = (hand - shoulder).GetSquaredLength();
					float const bendlen = sqrtf(std::max(armlen * armlen - distSqr * 0.25F, 0.0F));
					Vector3 const elbow = ((hand + shoulder) * 0.5F) + (bend * bendlen);

					addModel(*armModel, hand, elbow);
					addModel(*upperModel, elbow, shoulder);
				}
			}

			// --- local view ends
		}

		void ClientPlayer::AddToSceneThirdPersonView() {
			Player& p = player;
			Weapon& w = p.GetWeapon();
			IRenderer& renderer = client.GetRenderer();
			World* world = client.GetWorld();

			std::string modelPath = "Models/Player/";
			if (!cg_classicPlayerModels)
				modelPath += w.GetName() + "/";

			Vector3 o = p.GetFront(cg_orientationSmoothing); // interpolated
			Vector3 front2D = MakeVector3(o.x, o.y, 0).Normalize();
			Vector3 right = -Vector3::Cross(MakeVector3(0, 0, -1), front2D).Normalize();

			Handle<IModel> model;
			ModelRenderParam param;
			param.customColor = ConvertColorRGB(p.GetColor());

			if (!p.IsAlive()) {
				if (!cg_ragdoll) {
					model = renderer.RegisterModel((modelPath + "Dead.kv6").c_str());
					param.matrix = Matrix4::FromAxis(-right, front2D,
						MakeVector3(0, 0, 1), p.GetEye());
					param.matrix = param.matrix * Matrix4::Scale(0.1F);
					renderer.RenderModel(*model, param);
				}

				return;
			}

			Vector3 const origin = p.GetOrigin();

			// Set clipping region to prevent overdraw
			{
				Vector3 const outset(2.0F, 2.0F, 4.0F);

				sandboxedRenderer->SetClipBox(AABB3(origin - outset, origin + outset));
				sandboxedRenderer->SetAllowDepthHack(false);
			}

			// ready for tool rendering
			asIScriptObject* curSkin = GetCurrentSkin(false);
			SetSkinParameterForTool(currentTool, curSkin);
			SetCommonSkinParameter(curSkin);

			float pitchBias;
			{
				ScriptIThirdPersonToolSkin interface(curSkin);
				pitchBias = interface.GetPitchBias();
			}

			float yaw = atan2f(o.y, o.x) + M_PI_F * 0.5F;
			float pitch = -atan2f(o.z, o.GetLength2D());

			// lower axis
			Matrix4 const lower = Matrix4::Translate(origin)
				* Matrix4::Rotate(MakeVector3(0, 0, 1), yaw);

			Matrix4 const scaler = Matrix4::Scale(0.1F)
				* Matrix4::Scale(-1, -1, 1);

			PlayerInput const inp = p.GetInput();

			float const legsPosY = inp.crouch ? 0.25F : 0.0F;
			float const legsPosZ = inp.crouch ? 0.05F : 0.1F;
			float const headPosZ = inp.crouch ? 0.05F : 0.0F;
			float const torsoPosZ = inp.crouch ? 0.5F : 1.0F;
			float const armsPosZ = inp.crouch ? 0.0F : 0.1F;

			float armPitch = pitch;

			// slow pulse
			{
				float sp = 1.0F - aimDownState;
				float vl = sinf(time * 1.0F);

				pitch -= vl * 0.005F * sp;
				armPitch += vl * 0.0075F * sp;
			}

			if (inp.sprint)
				armPitch -= 0.9F * sprintState;

			// Moving this to the scripting environment means
			// breaking compatibility with existing scripts.
			WeaponInput actualWeapInput = p.GetWeaponInput();

			const float primaryDelay = p.GetToolPrimaryDelay();
			const float secondaryDelay = p.GetToolSecondaryDelay();

			if (currentTool == Player::ToolSpade) {
				const float nextSpadeTime = p.GetTimeToNextSpade();
				const float nextDigTime = p.GetTimeToNextDig();
				if (nextSpadeTime > 0.0F)
					armPitch -= (nextSpadeTime / primaryDelay);
				else if (actualWeapInput.secondary && nextDigTime > 0.0F)
					armPitch -= 1.0F - (nextDigTime / secondaryDelay);
			} else if (currentTool == Player::ToolBlock) {
				const float nextBlockTime = p.GetTimeToNextBlock();
				if (nextBlockTime > 0.0F)
					armPitch -= (nextBlockTime / primaryDelay);
			} else if (currentTool == Player::ToolWeapon) {
				const float nextFireTime = w.GetTimeToNextFire();
				if (nextFireTime > 0.0F)
					armPitch += nextFireTime;
			} else if (currentTool == Player::ToolGrenade) {
				const float fuse = p.GetGrenadeCookTime();
				if (p.IsCookingGrenade() && fuse > 0.0F)
					armPitch += fuse * DEG2RAD(30);
			}

			armPitch += pitchBias;
			if (armPitch < 0.0F)
				armPitch = std::max(armPitch, -M_PI_F * 0.5F) * 0.9F;

			Vector3 v = p.GetVelocity();
			Vector2 legsRot;
			legsRot.x = Vector3::Dot(v, p.GetFront2D());
			legsRot.y = Vector3::Dot(v, p.GetRight());
			legsRot *= sinf(p.GetWalkAnimationProgress() * M_PI_F * 2.0F) * 3.0F;

			Matrix4 const leg1 = lower
				* Matrix4::Translate(0.25F, legsPosY, -legsPosZ)
				* Matrix4::Rotate(MakeVector3(1, 0, 0), legsRot.x)
				* Matrix4::Rotate(MakeVector3(0, 1, 0), legsRot.y);

			Matrix4 const leg2 = lower
				* Matrix4::Translate(-0.25F, legsPosY, -legsPosZ)
				* Matrix4::Rotate(MakeVector3(1, 0, 0), -legsRot.x)
				* Matrix4::Rotate(MakeVector3(0, 1, 0), -legsRot.y);

			Matrix4 const torso = lower
				* Matrix4::Translate(0.0F, 0.0F, -torsoPosZ);

			Matrix4 const head = torso
				* Matrix4::Translate(0.0F, 0.0F, -headPosZ)
				* Matrix4::Rotate(MakeVector3(1, 0, 0), pitch);

			Matrix4 const arms = torso
				* Matrix4::Translate(0.0F, 0.0F, armsPosZ)
				* Matrix4::Rotate(MakeVector3(1, 0, 0), armPitch);

			// Legs
			{
				model = inp.crouch
					? renderer.RegisterModel((modelPath + "LegCrouch.kv6").c_str())
					: renderer.RegisterModel((modelPath + "Leg.kv6").c_str());

				param.matrix = leg1 * scaler;
				renderer.RenderModel(*model, param);

				param.matrix = leg2 * scaler;
				renderer.RenderModel(*model, param);
			}

			// Torso
			{
				model = inp.crouch
					? renderer.RegisterModel((modelPath + "TorsoCrouch.kv6").c_str())
					: renderer.RegisterModel((modelPath + "Torso.kv6").c_str());

				param.matrix = torso * scaler;
				renderer.RenderModel(*model, param);
			}

			// Arms
			{
				model = renderer.RegisterModel((modelPath + "Arms.kv6").c_str());

				param.matrix = arms * scaler;
				renderer.RenderModel(*model, param);
			}

			// Head
			{
				model = renderer.RegisterModel((modelPath + "Head.kv6").c_str());

				param.matrix = head * scaler;
				renderer.RenderModel(*model, param);
			}

			// Tool
			{
				ScriptIThirdPersonToolSkin interface(curSkin);
				interface.SetOriginMatrix(arms);
			}
			{
				ScriptIToolSkin interface(curSkin);
				interface.AddToScene();
			}

			hasValidOriginMatrix = true;

			// draw intel in ctf
			stmp::optional<IGameMode&> mode = world->GetMode();
			if (mode && mode->ModeType() == IGameMode::m_CTF) {
				auto& ctf = static_cast<CTFGameMode&>(mode.value());
				if (ctf.PlayerHasIntel(p)) {
					model = renderer.RegisterModel("Models/MapObjects/Intel.kv6");
					param.customColor = ConvertColorRGB(world->GetTeamColor(1 - p.GetTeamId()));
					Matrix4 const briefcase = torso
						* (inp.crouch ? Matrix4::Translate(0, 0.8F, 0.4F)
							* Matrix4::Rotate(MakeVector3(1, 0, 0), -0.5F)
						: Matrix4::Translate(0, 0.35F, 0.7F));
					param.matrix = briefcase * scaler;
					renderer.RenderModel(*model, param);
				}
			}

			// third person player rendering, done
		}

		void ClientPlayer::AddToScene() {
			SPADES_MARK_FUNCTION();

			Player& p = player;

			hasValidOriginMatrix = false;

			if (p.IsSpectator())
				return; // spectator

			// Do not draw a player with an invalid state
			if (p.GetFront().GetSquaredLength() < 0.01F)
				return;

			// distance cull
			const auto& viewOrigin = client.GetLastSceneDef().viewOrigin;
			float distSqr = (p.GetOrigin() - viewOrigin).GetSquaredLength2D();
			if (distSqr > FOG_DISTANCE_SQ)
				return;

			bool isThirdPerson = ShouldRenderInThirdPersonView();
			if (!isThirdPerson)
				AddToSceneFirstPersonView();
			else
				AddToSceneThirdPersonView();

			if (cg_debugToolSkinAnchors && currentTool == Player::ToolWeapon && p.IsLocalPlayer()) {
				IRenderer& renderer = client.GetRenderer();

				auto drawAxes = [&renderer](Vector3 p) {
					renderer.AddDebugLine(Vector3{p.x - 0.2F, p.y, p.z},
					                      Vector3{p.x + 0.2F, p.y, p.z},
					                      Vector4{1.0F, 0.0F, 0.0F, 1.0F});
					renderer.AddDebugLine(Vector3{p.x, p.y - 0.2F, p.z},
					                      Vector3{p.x, p.y + 0.2F, p.z},
					                      Vector4{0.0F, 0.6F, 0.0F, 1.0F});
					renderer.AddDebugLine(Vector3{p.x, p.y, p.z - 0.2F},
					                      Vector3{p.x, p.y, p.z + 0.2F},
					                      Vector4{0.0F, 0.0F, 1.0F, 1.0F});
				};

				drawAxes(isThirdPerson ? GetMuzzlePosition() : GetMuzzlePositionInFirstPersonView());
				drawAxes(isThirdPerson ? GetCaseEjectPosition() : GetCaseEjectPositionInFirstPersonView());
			}
		}

		void ClientPlayer::Draw2D() {
			if (!ShouldRenderInThirdPersonView() && player.IsAlive()) {
				asIScriptObject* curSkin = GetCurrentSkin(true);
				SetSkinParameterForTool(currentTool, curSkin);
				SetCommonSkinParameter(curSkin);

				// common process
				{
					ScriptIViewToolSkin interface(curSkin);
					interface.SetEyeMatrix(GetEyeMatrix());
					interface.SetSwing(viewWeaponOffset);
					interface.Draw2D();
				}
			}
		}

		bool ClientPlayer::ShouldRenderInThirdPersonView() {
			// The player from whom's perspective the game is
			return !client.IsInFirstPersonView(player.GetId());
		}

		Vector3 ClientPlayer::GetMuzzlePosition() {
			ScriptIWeaponSkin3 interface(weaponSkin);
			if (interface.ImplementsInterface()) {
				Vector3 muzzle = interface.GetMuzzlePosition();

				// The skin should return a legit position. Return the default position if it didn't.
				Vector3 const origin = player.GetOrigin();
				AABB3 clip = AABB3(origin - Vector3(2.0F, 2.0F, 4.0F), origin + Vector3(2.0F, 2.0F, 2.0F));
				if (clip.Contains(muzzle))
					return muzzle;
			}

			return (GetEyeMatrix() * MakeVector3(-0.13F, 1.5F, 0.2F)).GetXYZ();
		}

		Vector3 ClientPlayer::GetMuzzlePositionInFirstPersonView() {
			ScriptIWeaponSkin3 interface(weaponViewSkin);
			if (interface.ImplementsInterface())
				return interface.GetMuzzlePosition();

			return (GetEyeMatrix() * MakeVector3(-0.13F, 1.5F, 0.2F)).GetXYZ();
		}

		Vector3 ClientPlayer::GetCaseEjectPosition() {
			ScriptIWeaponSkin3 interface(weaponSkin);
			if (interface.ImplementsInterface()) {
				Vector3 caseEject = interface.GetCaseEjectPosition();

				// The skin should return a legit position. Return the default position if it didn't.
				Vector3 const origin = player.GetOrigin();
				AABB3 clip = AABB3(origin - Vector3(2.0F, 2.0F, 4.0F), origin + Vector3(2.0F, 2.0F, 2.0F));
				if (clip.Contains(caseEject))
					return caseEject;
			}

			return (GetEyeMatrix() * MakeVector3(-0.13F, 0.5F, 0.2F)).GetXYZ();
		}

		Vector3 ClientPlayer::GetCaseEjectPositionInFirstPersonView() {
			ScriptIWeaponSkin3 interface(weaponViewSkin);
			if (interface.ImplementsInterface())
				return interface.GetCaseEjectPosition();
			return (GetEyeMatrix() * MakeVector3(-0.13F, 0.5F, 0.2F)).GetXYZ();
		}

		struct ClientPlayer::AmbienceInfo {
			float room, size, distance;
		};

		ClientPlayer::AmbienceInfo ClientPlayer::ComputeAmbience() {
			const auto& viewOrigin = client.GetLastSceneDef().viewOrigin;
			const auto& rayFrom = player.GetEye();

			if (!cg_environmentalAudio) {
				AmbienceInfo result;
				result.room = 0.0F;
				result.distance = (viewOrigin - rayFrom).GetLength();
				result.size = 0.0F;
				return result;
			}

			float maxDistance = 40.0F;
			GameMap& map = *client.map;

			// uniformly distributed random unit vectors
			const Vector3 directions[24] = {
			  {-0.4806003057749437F, -0.42909622618705534F, 0.7647874049440525F},
			  {-0.32231294555647927F, 0.6282069816346844F, 0.7081457147735524F},
			  {0.048740582496498826F, -0.6670915238644523F, 0.7433796166200044F},
			  {0.4507022412112344F, 0.2196054264547812F, 0.8652403980621708F},
			  {-0.42721511627413183F, -0.587164590982542F, -0.6875499891085622F},
			  {-0.5570464880797501F, 0.3832470400156089F, -0.7367638131974799F},
			  {0.4379032819319448F, -0.5217172826725083F, -0.732155579528044F},
			  {0.5505793235065188F, 0.5884516130938041F, -0.5921039668625805F},
			  {0.681714179159347F, -0.6289005125058891F, -0.3738314102679548F},
			  {0.882424317058847F, 0.4680895178240496F, -0.047111866514457174F},
			  {0.8175844570742612F, -0.5123280060684333F, 0.26282250616819125F},
			  {0.7326555076593512F, 0.16938649523355995F, 0.6591844372623717F},
			  {-0.8833847855718798F, -0.46859333747646814F, -0.007183640636104698F},
			  {-0.6478926243769724F, 0.5325399055055595F, -0.5446433661783178F},
			  {-0.7011236289377749F, -0.4179353735633245F, 0.5777159167528706F},
			  {-0.8834742898471629F, 0.3226030059694268F, 0.3397064611080296F},
			  {-0.701272268659947F, 0.7126868112640804F, -0.017167243773185584F},
			  {-0.4048459451282839F, 0.8049148135357349F, 0.4338339586338529F},
			  {0.10511344475950758F, 0.7400485819463978F, -0.664288536774432F},
			  {0.4228172536676786F, 0.7759558485735245F, 0.46810051384874957F},
			  {-0.641642302739998F, -0.7293326298605313F, -0.23742171416118207F},
			  {-0.269582155924164F, -0.957885171758109F, 0.09890125850168793F},
			  {0.09274966874325204F, -0.9126579244190587F, -0.39806156803076687F},
			  {0.49359438685568013F, -0.721891173178783F, 0.48501310843226225F}
			};

			std::array<float, 24> distances;
			std::array<float, 24> feedbacknesses;

			std::fill(feedbacknesses.begin(), feedbacknesses.end(), 0.0F);

			for (std::size_t i = 0; i < distances.size(); ++i) {
				float& distance = distances[i];
				float& feedbackness = feedbacknesses[i];

				const Vector3& rayTo = directions[i];

				IntVector3 hitPos;
				if (map.CastRay(rayFrom, rayTo, maxDistance, hitPos)) {
					distance = (MakeVector3(hitPos) - rayFrom).GetLength();
					feedbackness = map.CastRay(rayFrom, -rayTo, maxDistance, hitPos) ? 1.0F : 0.0F;
				} else {
					distance = maxDistance * 2.0F;
				}
			}

			// monte-carlo integration
			unsigned int rayHitCount = 0;
			float roomSize = 0.0F;
			float feedbackness = 0.0F;

			for (float dist : distances) {
				if (dist < maxDistance) {
					rayHitCount++;
					roomSize += dist;
				}
			}

			for (float fb : feedbacknesses)
				feedbackness += fb;

			float reflections;
			if (rayHitCount > distances.size() / 4) {
				roomSize /= (float)rayHitCount;
				reflections = (float)rayHitCount / (float)distances.size();
			} else {
				reflections = 0.1F;
				roomSize = 100.0F;
			}

			feedbackness /= (float)distances.size();
			feedbackness = std::min(std::max(0.0F, feedbackness - 0.35F) / 0.5F, 1.0F);

			AmbienceInfo result;
			result.room = reflections * feedbackness;
			result.distance = (viewOrigin - rayFrom).GetLength();
			result.size = std::max(std::min(roomSize / 15.0F, 1.0F), 0.0F);
			result.room *= std::max(0.0F, std::min((result.size - 0.1F) * 4.0F, 1.0F));
			result.room *= 1.0F - result.size * 0.3F;

			return result;
		}

		void ClientPlayer::FiredWeapon() {
			Player& p = player;

			bool isThirdPerson = ShouldRenderInThirdPersonView();

			Vector3 muzzle = isThirdPerson
				? GetMuzzlePosition()
				: GetMuzzlePositionInFirstPersonView();

			// make dlight
			client.MuzzleFire(muzzle);

			// sound ambience estimation
			auto ambience = ComputeAmbience();

			// FIXME: what if current tool isn't weapon?
			asIScriptObject* skin = isThirdPerson ? weaponSkin : weaponViewSkin;

			{
				ScriptIWeaponSkin2 interface(skin);
				if (interface.ImplementsInterface()) {
					interface.SetSoundEnvironment(ambience.room, ambience.size, ambience.distance);
					interface.SetSoundOrigin(p.GetEye());
				} else if (isThirdPerson && !hasValidOriginMatrix) {
					// Legacy skin scripts rely on OriginMatrix which is only updated when
					// the player's location is within the fog range.
					return;
				}
			}

			{
				ScriptIWeaponSkin interface(skin);
				interface.WeaponFired();
			}
		}

		void ClientPlayer::EjectedBrass() {
			IRenderer& renderer = client.GetRenderer();
			IAudioDevice& audioDevice = client.GetAudioDevice();
			Player& p = player;

			// distance cull
			const auto& viewOrigin = client.GetLastSceneDef().viewOrigin;
			float distSqr = (p.GetOrigin() - viewOrigin).GetSquaredLength2D();
			if (distSqr > FOG_DISTANCE_SQ)
				return;

			Handle<IModel> model;
			Handle<IAudioChunk> snd = NULL;
			Handle<IAudioChunk> snd2 = NULL;
			switch (p.GetWeapon().GetWeaponType()) {
				case RIFLE_WEAPON:
					model = renderer.RegisterModel("Models/Weapons/Rifle/Casing.kv6");
					snd = SampleRandomBool()
					        ? audioDevice.RegisterSound("Sounds/Weapons/Rifle/ShellDrop1.opus")
					        : audioDevice.RegisterSound("Sounds/Weapons/Rifle/ShellDrop2.opus");
					snd2 = audioDevice.RegisterSound("Sounds/Weapons/Rifle/ShellWater.opus");
					break;
				case SHOTGUN_WEAPON:
					model = renderer.RegisterModel("Models/Weapons/Shotgun/Casing.kv6");
					break;
				case SMG_WEAPON:
					model = renderer.RegisterModel("Models/Weapons/SMG/Casing.kv6");
					snd = SampleRandomBool()
					        ? audioDevice.RegisterSound("Sounds/Weapons/SMG/ShellDrop1.opus")
					        : audioDevice.RegisterSound("Sounds/Weapons/SMG/ShellDrop2.opus");
					snd2 = audioDevice.RegisterSound("Sounds/Weapons/SMG/ShellWater.opus");
					break;
			}

			if (model) {
				Vector3 origin = ShouldRenderInThirdPersonView()
					? GetCaseEjectPosition()
					: GetCaseEjectPositionInFirstPersonView();

				Vector3 o = p.GetFront();
				Vector3 vel = o * 0.5F + p.GetRight() + p.GetUp() * 0.2F;
				switch (p.GetWeapon().GetWeaponType()) {
					case SMG_WEAPON: vel -= o * 0.7F; break;
					case SHOTGUN_WEAPON: vel *= 0.5F; break;
					default: break;
				}

				auto ent = stmp::make_unique<GunCasing>(&client,
					model.GetPointerOrNull(), snd.GetPointerOrNull(),
					snd2.GetPointerOrNull(), origin, o, vel);

				client.AddLocalEntity(std::move(ent));
			}
		}

		void ClientPlayer::ReloadingWeapon() {
			bool isThirdPerson = ShouldRenderInThirdPersonView();

			// FIXME: what if current tool isn't weapon?
			asIScriptObject* skin = isThirdPerson ? weaponSkin : weaponViewSkin;

			// sound ambience estimation
			auto ambience = ComputeAmbience();

			{
				ScriptIWeaponSkin2 interface(skin);
				if (interface.ImplementsInterface()) {
					interface.SetSoundEnvironment(ambience.room, ambience.size, ambience.distance);
					interface.SetSoundOrigin(player.GetEye());
				} else if (isThirdPerson && !hasValidOriginMatrix) {
					// Legacy skin scripts rely on OriginMatrix which is only updated when
					// the player's location is within the fog range.
					return;
				}
			}

			{
				ScriptIWeaponSkin interface(skin);
				interface.ReloadingWeapon();
			}
		}

		void ClientPlayer::ReloadedWeapon() {
			// FIXME: what if current tool isn't weapon?
			asIScriptObject* skin = ShouldRenderInThirdPersonView() ? weaponSkin : weaponViewSkin;

			{
				ScriptIWeaponSkin interface(skin);
				interface.ReloadedWeapon();
			}
		}
	} // namespace client
} // namespace spades