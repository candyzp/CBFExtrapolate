#include "collisions.hpp"

#include <Geode/Enums.hpp>
#include <Geode/Geode.hpp>
#include <algorithm>

#include "bot/bot.hpp"
#include "gravity.hpp"
#include "player.hpp"
#include "trajectory/trajectory.hpp"

namespace phys {

int checkPlayerCollisions(GJBaseGameLayer *gameLayer, PlayerObject *player) {
    player->m_wasTeleported = false;
    player->m_ringJumpRelated = false;
    player->m_collidedTopMinY = 0.0;
    player->m_collidedBottomMaxY = 0.0;
    player->m_collidedLeftMaxX = 0.0;
    player->m_collidedRightMinX = 0.0;

    player->m_wasOnSlope = player->m_isOnSlope;
    player->m_isOnSlope = false;

    bool isOnGround = player->m_isOnGround2;
    player->m_isOnGround4 = isOnGround;
    
    if (isOnGround && !player->m_platformerMovingLeft &&
        !player->m_platformerMovingRight && player->m_maybeSlidingTime > 0) {
        player->m_maybeSlidingTime = 0;
        player->m_maybeSlidingStartTime = player->m_totalTime;
    }

    if (player->m_unk669) {
        player->m_currentPotentialSlope = nullptr;
    }
    
    player->m_unk669 = true;
    player->m_potentialSlopeMap.clear();
    
    if (GameObject* currentSlope = player->m_currentPotentialSlope) {
        player->m_potentialSlopeMap.insert({currentSlope->m_uniqueID, currentSlope});
    }

    if (GameObject* currentSlope = player->m_currentSlope) {
        player->m_potentialSlopeMap.insert({currentSlope->m_uniqueID, currentSlope});
    }

    float someValue = 0.0;
    float vehicleSize = player->m_vehicleSize;
    float unkAngle = player->m_unkAngle1;
    
    if (vehicleSize != 1.0) {
        someValue = (1.0 - vehicleSize) * unkAngle * 0.5;
    }
    
    float unkAngleHalved = unkAngle * 0.5;
    float angleTransformed = (unkAngleHalved + 90.0) - someValue;

    bool groundExists =
        player->m_isShip || player->m_isBird || player->m_isDart ||
        player->m_isSwing || player->m_isBall || player->m_isSpider ||
        gameLayer->m_gameState.m_isDualMode;

    player->m_isOutOfBounds = false;
    bool exceededBounds = false;

    auto playerPosition = player->getPosition();

    if (gameLayer->m_isPlatformer) {
        if (playerPosition.x < -30.0) {
            player->m_platformerXVelocity = 0.0;
            player->setPosition({-30.0, playerPosition.y});
        }
    } else if (player->m_isGoingLeft) {
        exceededBounds = (playerPosition.x < -30.0);
    }

    if (angleTransformed <= playerPosition.y || (groundExists)) {
        if (playerPosition.y > (float)(someValue + gameLayer->m_maxGameplayY)) {
            exceededBounds = true;
        }
    } else if (player->m_isUpsideDown) {
        if (player->m_lastFlipTime != 0 && player->m_totalTime - player->m_lastFlipTime < 0.1) {
            player->setPosition({-1.0842022e-19f, playerPosition.x});
            player->hitGround(nullptr, true);
            player->updateCollide(PlayerCollisionDirection::Top, nullptr);
            player->m_isOnGround2 = false;
            return 0;
        }
        exceededBounds = true;
    }

    // TODO: implement missing teleport logic handling
    return 0;
}

int collidedWithObjectInternal(PlayerObject *player, float, GameObject *object, CCRect *p4, bool) {
    bool holdingLeft = player->m_holdingLeft;
    bool holdingRight = player->m_holdingRight;
    
    if (player->m_leftPressedFirst) {
        holdingRight = false;
    } else {
        holdingLeft = false;
    }

    cocos2d::CCRect playerRect;
    if (p4->equals(cocos2d::CCRect{0.0, 0.0, 0.0, 0.0})) {
        playerRect = player->getObjectRect();
        if (object) {
            *p4 = object->getObjectRect();
            playerRect = object->getObjectRect(); 
        }
    } else {
        playerRect = player->getObjectRect();
    }

    float unk1 = 5.0;
    if (!player->m_isPlatformer || player->m_wasOnSlope || player->m_isOnSlope) {
        unk1 = 10.0;
    }
    unk1 = player->m_stateScale > 0 ? 15.0 : unk1;

    double unk2 = player->m_isUpsideDown ? -unk1 : unk1;
    if ((player->m_isShip || player->m_isBird || player->m_isDart || player->m_isSwing) && !player->m_isPlatformer) {
        unk2 = player->m_isUpsideDown ? -6.0 : 6.0;
    }
    
    if (player->m_wasOnSlope) {
        unk2 = unk2 + (player->m_isUpsideDown ? -1.0 : 1.0) * player->unk_584;
    }

    return 0;
}

void preSlopeCollision(PlayerObject *player, float dt, GameObject *slope) {
    if (slope->m_uniqueID == player->m_collidingWithSlopeId)
        return;

    cocos2d::CCRect playerRect = player->getObjectRect();
    cocos2d::CCRect slopeRect = slope->getObjectRect();

    int slopeDir = slope->m_slopeDirection;
    bool unk1 = (slopeDir < 5 || slopeDir == 6);

    float unk3 = (!player->m_isPlatformer || (!player->m_wasOnSlope && !player->m_isOnSlope)) ? 0.0 : 5.0;

    cocos2d::CCPoint pos = slope->getRealPosition();
    cocos2d::CCPoint diff = pos - slope->m_lastPosition;

    if (player->m_isSideways) {
        diff.x = diff.y;
    }

    float playerSpeed = player->m_playerSpeed * player->m_speedMultiplier * dt;
    float slopePosX = slope->getPosition().x;
    
    if (unk1) {
        if ((!player->m_isGoingLeft || player->m_isPlatformer || diff.x < playerSpeed) && slopePosX < slopeRect.origin.x) {
            int unk4 = (!player->m_isGoingLeft || player->m_isPlatformer) ? 0 : 1;
            unk3 += unk4;
            float heightDiff = slopeRect.size.height - (unk3 * 2);
            cocos2d::CCRect newPlayerRect = {slopeRect.origin.x, unk3 + slopeRect.origin.y, 1.0, heightDiff};
            
            if (newPlayerRect.intersectsRect(playerRect)) {
                // Ensure properly passing address of rect if collidedWithObjectInternal expects pointer
                // Using player-> member depending on bindings, or namespace wrapper
                // player->collidedWithObjectInternal(dt, slope, &newPlayerRect, false);
            }
        }
    } else if (player->m_isGoingLeft || player->m_isPlatformer || (playerSpeed < diff.x)) {
        if (slopeRect.getMaxX() < slopePosX) {
            int unk4 = (!player->m_isGoingLeft && !player->m_isPlatformer) ? 1 : 0;
            float newX = (slopeRect.size.width + slopeRect.origin.x) - 1.0;
            unk3 += unk4;
            float heightDiff = slopeRect.size.height - (unk3 * 2);
            cocos2d::CCRect newPlayerRect = {newX, unk3 + slopeRect.origin.y, 1.0, heightDiff};
            
            if (newPlayerRect.intersectsRect(playerRect)) {
                // player->collidedWithObjectInternal(dt, slope, &newPlayerRect, false);
            }
        }
    }
}

void collidedWithSlopeInternal(PlayerObject *) {}

void activateForTrajectory(EffectGameObject *obj, PlayerObject *player) {
    Bot::get()->trajectory().rememberActivatedObject(obj, player);
}

void bumpPlayerFromGJBGL(GJBaseGameLayer *pl, PlayerObject *player, EffectGameObject *object) {
    if (pl->canBeActivatedByPlayer(player, object)) {
        player->m_lastPortalPos = object->getPosition();
        activateForTrajectory(object, player);
        float force = 1.0;

        if (object->m_objectType == GameObjectType::PinkJumpPad) {
            if (player->m_isShip) force = 0.35;
            else if (player->m_isBird) force = 0.4;
            else if (player->m_isBall || player->m_isSpider) force = 0.7;
            else force = 0.65;
        } else if (object->m_objectType == GameObjectType::RedJumpPad) {
            if (player->m_isShip) force = (player->m_vehicleSize >= 1.0) ? 0.63 : 0.95;
            else if (player->m_isBird) force = (player->m_vehicleSize >= 1.0) ? 0.6 : 0.98;
            else force = 1.25;
        }
        
        player->m_lastActivatedPortal = object;
        phys::bumpPlayer(player, force, static_cast<int>(object->m_objectType), false, object);
    }
}

static void *g_PlayerObject_getOrientedBox = nullptr;
static void *g_PlayerObject_updateOrientedBox = nullptr;

$execute {
    g_PlayerObject_getOrientedBox = nullptr;
    g_PlayerObject_updateOrientedBox = nullptr;
}

void collisionCheckObjects(GJBaseGameLayer *pl, PlayerObject *player, gd::vector<GameObject *> *objects, int objectCount, float dt, bool enableSolids) {
    if (!pl || !player || !objects || objectCount <= 0) return;

    const int availableObjects = static_cast<int>(objects->size());
    if (availableObjects <= 0) return;

    const int safeObjectCount = std::min(objectCount, availableObjects);
    
    // HOIST BOT CALL OUT OF THE LOOP FOR PERFORMANCE
    auto bot = Bot::get();
    auto& trajectory = bot->trajectory();
    const bool isFakePlayer = trajectory.isFakePlayer(player);

    CCRect playerRect = player->getObjectRect();

    for (int i = 0; i < safeObjectCount; i++) {
        GameObject *object = objects->at(i);
        if (!object) continue;

        auto objType = object->m_objectType;

        if (objType == GameObjectType::Decoration ||
            objType == GameObjectType::CollisionObject ||
            objType == GameObjectType::SecretCoin ||
            objType == GameObjectType::UserCoin ||
            objType == GameObjectType::Collectible ||
            objType == GameObjectType::EnterEffectObject ||
            object->m_objectID == 286 || object->m_objectID == 287 ||
            object->m_isGroupDisabled || object->m_isDisabled) {
            continue;
        }

        if (objType == GameObjectType::Solid || objType == GameObjectType::Breakable) {
            if (isFakePlayer && !enableSolids) continue;

            // SAFE VECTOR ACCESS - Prevent std::out_of_range crashes
            if (pl->m_solidCollisionObjectsCount < pl->m_solidCollisionObjects.size()) {
                pl->m_solidCollisionObjects[pl->m_solidCollisionObjectsCount] = object;
            } else {
                pl->m_solidCollisionObjects.push_back(object);
            }
            pl->m_solidCollisionObjectsCount++;
            continue;
        }

        if (object == pl->m_anticheatSpike) continue;

        if (objType == GameObjectType::Hazard || objType == GameObjectType::AnimatedHazard) {
            if (isFakePlayer) continue;
            
            // SAFE VECTOR ACCESS
            if (pl->m_hazardCollisionObjectsCount < pl->m_hazardCollisionObjects.size()) {
                pl->m_hazardCollisionObjects[pl->m_hazardCollisionObjectsCount] = object;
            } else {
                pl->m_hazardCollisionObjects.push_back(object);
            }
            pl->m_hazardCollisionObjectsCount++;
            continue;
        }

        EffectGameObject *obj = static_cast<EffectGameObject*>(object);
        
        if ((trajectory.playerHasActivated(player, obj) || trajectory.realPlayerHasActivated(player, obj)) && (objType != GameObjectType::Slope)) {
            continue;
        }

        cocos2d::CCRect rect = (objType == GameObjectType::Slope) ? object->getObjectRect(2.0, 2.0) : object->getObjectRect();

        if (object->m_objectRadius <= 0.0) {
            if (!playerRect.intersectsRect(rect)) continue;
        } else if (!pl->playerCircleCollision(player, object)) {
            continue;
        }

        bool overlaps = true;
        const bool needsOuterOBB = object->m_shouldUseOuterOb &&
            (!pl->m_levelSettings || !pl->m_levelSettings->m_fixRadiusCollision || object->m_objectRadius <= 0.0);

        if (needsOuterOBB && !isFakePlayer) {
            OBB2D *box = object->getOrientedBox();
            player->updateOrientedBox();
            OBB2D *playerBox = static_cast<GameObject *>(player)->getOrientedBox();
            if (box && playerBox) {
                overlaps = box->overlaps1Way(playerBox);
            }
        }

        if (!overlaps) continue;

        switch (objType) {
        case GameObjectType::InverseGravityPortal:
        case GameObjectType::NormalGravityPortal:
            player->m_lastPortalPos = object->getPosition();
            player->m_lastActivatedPortal = object;
            activateForTrajectory(obj, player);
            phys::flipGravity(player, objType == GameObjectType::InverseGravityPortal);
            break;
            
        case GameObjectType::GravityTogglePortal:
            player->m_lastPortalPos = object->getPosition();
            player->m_lastActivatedPortal = object;
            activateForTrajectory(obj, player);
            phys::flipGravity(player, !player->m_isUpsideDown);
            break;
            
        case GameObjectType::TeleportPortal:
            if (pl->canBeActivatedByPlayer(player, obj)) {
                phys::teleportPlayer(pl, static_cast<TeleportPortalObject*>(object), player);
                activateForTrajectory(obj, player);
            }
            break;
            
        case GameObjectType::Slope:
            if (!player->m_isSideways) {
                player->collidedWithSlopeInternal(dt, object, false);
            } else {
                cocos2d::CCRect emptyRect = cocos2d::CCRect{0.0, 0.0, 0.0, 0.0};
                player->handleRotatedCollisionInternal(dt, object, emptyRect, false, false, true);
            }
            break;
            
        case GameObjectType::CustomRing:
        case GameObjectType::DashRing:
        case GameObjectType::DropRing:
        case GameObjectType::GravityDashRing:
        case GameObjectType::GravityRing:
        case GameObjectType::GreenRing:
        case GameObjectType::PinkJumpRing:
        case GameObjectType::RedJumpRing:
        case GameObjectType::SpiderOrb:
        case GameObjectType::YellowJumpRing:
        case GameObjectType::TeleportOrb:
            if (!player->m_touchingRings->containsObject(object)) {
                player->m_touchingRings->addObject(object);
            }
            player->m_touchedRings.insert(object->m_uniqueID);

            if (!player->m_isShip && !player->m_isBird && !player->m_isDart && !player->m_isSwing && !static_cast<RingObject*>(object)->m_isSpawnOnly) {
                phys::ringJump(player, static_cast<RingObject*>(object));
                activateForTrajectory(obj, player);
            }
            break;
            
        case GameObjectType::YellowJumpPad:
        case GameObjectType::PinkJumpPad:
        case GameObjectType::RedJumpPad:
        case GameObjectType::SpiderPad:
            phys::bumpPlayerFromGJBGL(pl, player, obj);
            break;
            
        case GameObjectType::GravityPad: {
            bool isFacingDown = player->m_isSideways ? object->isFacingLeft() : object->isFacingDown();

            if (player->m_isUpsideDown == isFacingDown && pl->canBeActivatedByPlayer(player, obj)) {
                if (obj->m_isReverse) player->reversePlayer(obj);

                player->m_lastPortalPos = obj->getPosition();
                player->m_lastActivatedPortal = obj;
                activateForTrajectory(obj, player);

                phys::propellPlayer(player, 0.8, false, 10);
                phys::flipGravity(player, !isFacingDown);
                player->m_padRingRelated = true;
            }
            break;
        }
        case GameObjectType::MiniSizePortal:
        case GameObjectType::RegularSizePortal:
            if (pl->canBeActivatedByPlayer(player, obj)) {
                player->m_lastPortalPos = obj->getPosition();
                player->m_lastActivatedPortal = obj;
                activateForTrajectory(obj, player);
                phys::togglePlayerScale(player, objType == GameObjectType::MiniSizePortal);
            }
            break;
            
        case GameObjectType::Special:
            if (object->m_objectID == 0x743) player->m_stateHitHead = 2;
            else if (object->m_objectID == 0x6db) player->m_stateDartSlide = 2;
            else if (object->m_objectID == 0x715) player->m_stateNoAutoJump = 2;
            else if (object->m_objectID == 0x725 && player->m_isDashing) {
                phys::stopDashing(player);
                player->m_jumpBuffered = false;
            } else if (object->m_objectID == 0xb32) player->m_stateFlipGravity = 2;
            else if (object->m_objectID == 2069 || object->m_objectID == 3645) {
                player->m_stateForce = 2;
                ForceBlockGameObject *forceBlock = static_cast<ForceBlockGameObject*>(object);
                int forceID = forceBlock->m_forceID;
                
                if (forceID > 0) {
                    if (player->m_jumpPadRelated.count(forceID) && player->m_jumpPadRelated[forceID]) break;
                    player->m_jumpPadRelated[forceID] = true; // Optimization: avoids .insert() and duplicate lookups
                }

                player->m_stateForceVector += forceBlock->calculateForceToTarget(player);
            }
            break;
            
        case GameObjectType::CubePortal:
        case GameObjectType::ShipPortal:
        case GameObjectType::BallPortal:
        case GameObjectType::UfoPortal:
        case GameObjectType::WavePortal:
        case GameObjectType::SpiderPortal:
        case GameObjectType::SwingPortal:
        case GameObjectType::RobotPortal:
            activatingPortal(pl, player, obj);
            break;
            
        case GameObjectType::Modifier:
            obj->activatedByPlayer(player);
            if (obj->m_isTouchTriggered) phys::triggerObject(obj, pl, player);
            break;
            
        default:
            break;
        }
    }
}

void triggerObject(EffectGameObject *object, GJBaseGameLayer *pl, PlayerObject *player) {
    auto bot = Bot::get();
    auto unsafeInner = bot->trajectory().unsafeInner();
    bool isFake = (unsafeInner->m_fakePlayer1 == player || unsafeInner->m_fakePlayer2 == player);

    switch (object->m_objectID) {
        case 200: if (!isFake) *(float *)(&pl->m_gameState.m_timeModRelated) = 0.7f; break;
        case 201: if (!isFake) *(float *)(&pl->m_gameState.m_timeModRelated) = 0.9f; break;
        case 202: if (!isFake) *(float *)(&pl->m_gameState.m_timeModRelated) = 1.1f; break;
        case 203: if (!isFake) *(float *)(&pl->m_gameState.m_timeModRelated) = 1.3f; break;
        case 1334: if (!isFake) *(float *)(&pl->m_gameState.m_timeModRelated) = 1.6f; break;
        case 2066: {
            if (!object->m_followCPP) {
                bool isP2 = (unsafeInner->m_fakePlayer2 == player);
                if (object->m_targetPlayer2 == isP2) {
                    player->m_gravityMod = object->m_gravityValue;
                }
            }
            break;
        }
        case 2900: {
            RotateGameplayGameObject *rotateObj = static_cast<RotateGameplayGameObject*>(object);
            player->rotateGameplay(
                rotateObj->m_moveDirection, rotateObj->m_groundDirection,
                rotateObj->m_editVelocity, rotateObj->m_velocityModX,
                rotateObj->m_velocityModY, rotateObj->m_overrideVelocity,
                rotateObj->m_dontSlide);
            break;
        }
        case 3022:
            phys::teleportPlayer(pl, static_cast<TeleportPortalObject*>(object), player);
            break;
        default:
            break;
    }

    phys::activateForTrajectory(object, player);
}

void checkSpawnObjects(GJBaseGameLayer *pl, PlayerObject *player) {
    cocos2d::CCArray *objects = static_cast<cocos2d::CCArray*>(pl->m_spawnObjects->objectForKey(pl->m_gameState.m_currentChannel));
    if (!objects) return;

    // SAFE MAP ACCESS - Fixes standard library exceptions crashing the game
    int startingIndex = 0;
    auto it0 = pl->m_gameState.m_spawnChannelRelated0.find(pl->m_gameState.m_currentChannel);
    if (it0 != pl->m_gameState.m_spawnChannelRelated0.end()) {
        startingIndex = it0->second;
    }

    bool goingBack = false;
    auto it1 = pl->m_gameState.m_spawnChannelRelated1.find(pl->m_gameState.m_currentChannel);
    if (it1 != pl->m_gameState.m_spawnChannelRelated1.end()) {
        goingBack = it1->second;
    }

    CCPoint position = player->getPosition();
    auto bot = Bot::get();
    auto& trajectory = bot->trajectory(); // Store reference outside loop

    for (int i = startingIndex; static_cast<unsigned int>(i) < objects->count(); i++) {
        SpawnTriggerGameObject *object = static_cast<SpawnTriggerGameObject*>(objects->objectAtIndex(i));
        
        int objID = object->m_objectID;
        if (objID != 2066 && objID != 2900 && objID != 3022 && objID != 901) continue;

        CCPoint objectPos = object->m_speedStart;

        if (player->m_isSideways) {
            if (goingBack ? (objectPos.y < position.y) : (objectPos.y > position.y)) break;
        } else {
            if (goingBack ? (objectPos.x < position.x) : (objectPos.x > position.x)) break;
        }

        if (object->m_isGroupDisabled) continue;
        
        if (trajectory.playerHasActivated(player, object) || trajectory.realPlayerHasActivated(player, object)) continue;

        if (!object->m_isTouchTriggered) {
            phys::triggerObject(object, pl, player);
        }
    }
}
} // namespace phys
