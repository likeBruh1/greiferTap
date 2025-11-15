#include "Resolver.hpp"
#include "../Visuals/EventLogger.hpp"
#include "../Scripting/Scripting.hpp"
#include <sstream>
#include "Autowall.hpp"
#include "AntiAim.hpp"
#include "Ragebot.hpp"
#include <iomanip>
#include "BoneSetup.hpp"


#if defined(BETA_MODE) || defined(DEV)
//#define BLOOD_RESOLVER
#endif

Resolver g_Resolver;

void Resolver::CorrectShotRecord( LagRecord_t *record ) {
	Animations::AnimationEntry_t *data = g_Animations.GetAnimationEntry( record->m_nEntIndex );
	if( !data )
		return;

	auto *weapon = ( C_WeaponCSBaseGun * )data->m_pEntity->m_hActiveWeapon( ).Get( );
	if( !weapon )
		return;

	const auto shot_time = weapon->m_fLastShotTime( );
	const auto shot_tick = TIME_TO_TICKS( shot_time );
	const auto sim_tick = TIME_TO_TICKS( record->m_flSimulationTime );
	const auto anim_tick = TIME_TO_TICKS( record->m_flAnimationTime );

	// player fired this tick
	if( shot_tick == sim_tick && record->m_nChokedTicks < 2 ) {
		// dont need to fix pitch here, they should have onshot.
		record->m_bFixingPitch = false;
		record->m_bShooting = record->m_bSimTick = true;
	}
	else {
		if( shot_tick > anim_tick && sim_tick > shot_tick ) {
			record->m_bShooting = record->m_bFixingPitch = true;
		}
		else if( shot_tick == anim_tick ) {
			if( shot_tick + 1 == sim_tick ) {
				record->m_bShooting = true;
				record->m_bFixingPitch = false;
				return;
			}

			// fix it
			record->m_bShooting = record->m_bFixingPitch = true;
		}
	}

	if( record->m_bFixingPitch ) {
		float valid_pitch = 89.f;

		for( auto it = data->m_deqRecords.begin( ); it != data->m_deqRecords.end( ); it = next( it ) ) {
			if( !&it )
				continue;

			if( &*it == record || it->m_pEntity->IsDormant( ) )
				continue;

			if( !it->m_bFixingPitch && !record->m_bSimTick ) {
				valid_pitch = it->m_angEyeAngles.x;
				break;
			}
		}

		record->m_angEyeAngles.x = valid_pitch;
	}
}

LagRecord_t Resolver::FindIdealRecord( Animations::AnimationEntry_t *data, float flTargetDamage ) {
	if( data->m_deqRecords.empty( ) || !data->m_pEntity )
		return {};

	const auto pLocal = C_CSPlayer::GetLocalPlayer( );
	if( !pLocal || pLocal->IsDead( ) || !g_Ragebot.m_AimbotInfo.m_pSettings )
		return {};

	// we might still want to get this record outside of aimbot, so just do this as a sort of fix to still get accurate damage (and not crash)
	if( flTargetDamage == 1337.f )
		flTargetDamage = g_Ragebot.m_AimbotInfo.m_pSettings->minimal_damage;

	LagRecord_t pBestRecord;
	pBestRecord.m_pEntity = nullptr;

	bool bFoundIdealRecord = false;

	if( !bFoundIdealRecord ) {
		for( auto it = data->m_deqRecords.rbegin( ); it != data->m_deqRecords.rend( ); it = next( it ) ) {
			if( !it->IsRecordValid( ) || it->m_bInvalid || it->m_bGunGameImmunity )
				continue;

			if( g_Vars.rage.wait_for_lby_flick && g_Vars.rage.wait_for_lby_flick_key.enabled ) {
				if( !it->m_bLBYFlicked )
					continue;
			}

			auto pNextRecord = ( it + 1 );
			auto pPrevRecord = ( it - 1 );

			bool bNextValid = pNextRecord != data->m_deqRecords.rend( ) && pNextRecord != data->m_deqRecords.rbegin( );
			bool bPrevValid = pPrevRecord != data->m_deqRecords.rend( ) && pPrevRecord != data->m_deqRecords.rbegin( );

			// rip nothing to compare with...
			if( !bNextValid || !bPrevValid ) {
				break;
			}

			if( !pPrevRecord->IsRecordValid( ) )
				continue;

			if( !pNextRecord->IsRecordValid( ) )
				continue;

			// nah don't bother, all need to be 'resolved'
			if( it->m_vecPredVelocity.Length2D( ) < 1.f ||
				pNextRecord->m_vecPredVelocity.Length2D( ) < 1.f ||
				pPrevRecord->m_vecPredVelocity.Length2D( ) < 1.f ) {
				break;
			}

			float flAverageDelta = 0.f;
			flAverageDelta += fabsf( Math::AngleDiff( it->m_flLowerBodyYawTarget, pNextRecord->m_flLowerBodyYawTarget ) );
			flAverageDelta += fabsf( Math::AngleDiff( it->m_flLowerBodyYawTarget, pPrevRecord->m_flLowerBodyYawTarget ) );
			flAverageDelta += fabsf( Math::AngleDiff( pNextRecord->m_flLowerBodyYawTarget, pPrevRecord->m_flLowerBodyYawTarget ) );

			flAverageDelta /= 3.f;

			// look for low angle change between all these records
			// static angle prefer this one :3
			if( flAverageDelta < 35.f ) {
				LagRecord_t backup;
				backup.SetupRecord( data->m_pEntity, true );

				// for le tracing...	
				it->ApplyRecord( data->m_pEntity );

				// start parameters
				penetration::PenetrationInput_t in;
				in.m_start = g_Ragebot.m_AimbotInfo.m_vecEyePosition;
				in.m_damage = flTargetDamage;
				in.m_can_pen = true;
				in.m_target = data->m_pEntity;
				in.m_from = g_Ragebot.m_AimbotInfo.m_pLocal;

				penetration::PenetrationOutput_t out;

				bool bValidDamage = false;

				// just choose last record
				if( g_Ragebot.m_AimbotInfo.m_pSettings->accuracy_boost == 0 ) {
					it->m_nIdeality = 2;
					pBestRecord = *it;
					backup.ApplyRecord( data->m_pEntity );
					bFoundIdealRecord = true;
					break;
				}
				// medium, scan only some hitboxes for records
				else if( g_Ragebot.m_AimbotInfo.m_pSettings->accuracy_boost == 1 ) {
					// try head first
					in.m_pos = it->m_sAnims[ ESides::SIDE_SERVER ].m_pMatrix[ 8 ].at( 3 );
					bValidDamage = penetration::run( &in, &out );

					// try stomach
					if( !bValidDamage ) {
						in.m_pos = it->m_sAnims[ ESides::SIDE_SERVER ].m_pMatrix[ 3 ].at( 3 );
						bValidDamage = penetration::run( &in, &out );
					}

				}
				// high, scan most hitboxes for records
				else if( g_Ragebot.m_AimbotInfo.m_pSettings->accuracy_boost == 2 ) {
					// try head first
					in.m_pos = it->m_sAnims[ ESides::SIDE_SERVER ].m_pMatrix[ 8 ].at( 3 );
					bValidDamage = penetration::run( &in, &out );

					// try stomach
					if( !bValidDamage ) {
						in.m_pos = it->m_sAnims[ ESides::SIDE_SERVER ].m_pMatrix[ 3 ].at( 3 );
						bValidDamage = penetration::run( &in, &out );

						// try left foot
						if( !bValidDamage ) {
							in.m_pos = it->m_sAnims[ ESides::SIDE_SERVER ].m_pMatrix[ 74 ].at( 3 );
							bValidDamage = penetration::run( &in, &out );

							// try right foot
							if( !bValidDamage ) {
								in.m_pos = it->m_sAnims[ ESides::SIDE_SERVER ].m_pMatrix[ 67 ].at( 3 );
								bValidDamage = penetration::run( &in, &out );
							}
						}
					}
				}

				backup.ApplyRecord( data->m_pEntity );
				if( bValidDamage ) {
					it->m_nIdeality = 2;
					pBestRecord = *it;
					bFoundIdealRecord = true;
					break;
				}
			}
		}
	}

	// continue looking for some other nice record
	if( !bFoundIdealRecord ) {
		// iterate records.
		for( auto it = data->m_deqRecords.rbegin( ); it != data->m_deqRecords.rend( ); it = next( it ) ) {
			if( !it->IsRecordValid( ) || it->m_bInvalid || it->m_bGunGameImmunity )
				continue;

			if( g_Vars.rage.wait_for_lby_flick && g_Vars.rage.wait_for_lby_flick_key.enabled ) {
				if( !it->m_bLBYFlicked )
					continue;
			}

			LagRecord_t backup;
			backup.SetupRecord( data->m_pEntity, true );

			// for le tracing...	
			it->ApplyRecord( data->m_pEntity );

			// start parameters
			penetration::PenetrationInput_t in;
			in.m_start = g_Ragebot.m_AimbotInfo.m_vecEyePosition;
			in.m_damage = flTargetDamage;
			in.m_can_pen = true;
			in.m_target = data->m_pEntity;
			in.m_from = g_Ragebot.m_AimbotInfo.m_pLocal;

			penetration::PenetrationOutput_t out;

			bool bValidDamage = false;

			// just choose last record
			if( g_Ragebot.m_AimbotInfo.m_pSettings->accuracy_boost == 0 ) {
				it->m_nIdeality = 1;
				pBestRecord = *it;
				backup.ApplyRecord( data->m_pEntity );
				bFoundIdealRecord = true;
				break;
			}
			// medium, scan only some hitboxes for records
			else if( g_Ragebot.m_AimbotInfo.m_pSettings->accuracy_boost == 1 ) {
				// try head first
				in.m_pos = it->m_sAnims[ ESides::SIDE_SERVER ].m_pMatrix[ 8 ].at( 3 );
				bValidDamage = penetration::run( &in, &out );

				// try stomach
				if( !bValidDamage ) {
					in.m_pos = it->m_sAnims[ ESides::SIDE_SERVER ].m_pMatrix[ 3 ].at( 3 );
					bValidDamage = penetration::run( &in, &out );
				}

			}
			// high, scan most hitboxes for records
			else if( g_Ragebot.m_AimbotInfo.m_pSettings->accuracy_boost == 2 ) {
				// try head first
				in.m_pos = it->m_sAnims[ ESides::SIDE_SERVER ].m_pMatrix[ 8 ].at( 3 );
				bValidDamage = penetration::run( &in, &out );

				// try stomach
				if( !bValidDamage ) {
					in.m_pos = it->m_sAnims[ ESides::SIDE_SERVER ].m_pMatrix[ 3 ].at( 3 );
					bValidDamage = penetration::run( &in, &out );

					// try left foot
					if( !bValidDamage ) {
						in.m_pos = it->m_sAnims[ ESides::SIDE_SERVER ].m_pMatrix[ 74 ].at( 3 );
						bValidDamage = penetration::run( &in, &out );

						// try right foot
						if( !bValidDamage ) {
							in.m_pos = it->m_sAnims[ ESides::SIDE_SERVER ].m_pMatrix[ 67 ].at( 3 );
							bValidDamage = penetration::run( &in, &out );
						}
					}
				}
			}

			backup.ApplyRecord( data->m_pEntity );
			if( bValidDamage ) {
				it->m_nIdeality = 1;
				pBestRecord = *it;
				bFoundIdealRecord = true;

				// found perfect record let's take it
				// if not, then let's keep searching
				if( it->m_bIsResolved || ( it->m_bShooting && it->m_bSimTick ) )
					break;
			}
		}
	}

	if( pBestRecord.m_bInvalid || !pBestRecord.m_pEntity || !bFoundIdealRecord ) {
		if( g_Vars.rage.wait_for_lby_flick && g_Vars.rage.wait_for_lby_flick_key.enabled ) {
			for( auto &rec : data->m_deqRecords ) {
				if( !rec.IsRecordValid( ) || rec.m_bInvalid || rec.m_bGunGameImmunity )
					continue;

				if( rec.m_bLBYFlicked ) {
					pBestRecord = rec;
					pBestRecord.m_nIdeality = 0;
					break;
				}
			}
		}
		else {

			pBestRecord = g_Animations.GetLatestRecord( data->m_pEntity->EntIndex( ) );
			pBestRecord.m_nIdeality = 0;
		}
	}

	// none found above, return the first valid record if possible.
	return pBestRecord;
}

void Resolver::OnSpawnBlood( C_TEEffectDispatch *pBlood ) {
	auto pLocal = C_CSPlayer::GetLocalPlayer( );
	if( !pLocal )
		return;

	auto pEntity = ( C_CSPlayer * )g_pEntityList->GetClientEntityFromHandle( pBlood->m_EffectData.m_hEntity );
	if( !pEntity )
		return;

	auto pState = pEntity->m_PlayerAnimState( );
	if( !pState )
		return;

	if( pEntity->EntIndex( ) <= 0 || pEntity->EntIndex( ) >= 65 )
		return;

	auto &data = GetResolverData( pEntity->EntIndex( ) );

	//	printf( "blood spawn time bro: %.4f\n", g_pGlobalVars->curtime );

	// no valid player_hurt data to match this shot with
	if( data.m_vecPlayerHurtInfo.empty( ) )
		return;

	int nMatchedHitgroup = -1;
	for( auto &playerHurt : data.m_vecPlayerHurtInfo ) {
		// improper/outdated match
		if( fabsf( playerHurt.m_flTime - g_pGlobalVars->curtime ) > TICKS_TO_TIME( 14 ) )
			continue;

		// yo we matched lel
		if( playerHurt.m_flTime == g_pGlobalVars->curtime ) {
			nMatchedHitgroup = playerHurt.m_nHitgroup;
			break;
		}
	}

	// failed to match this shot with a valid player_hurt event
	if( nMatchedHitgroup == -1 )
		return;

	CCSGOPlayerAnimState pStateBackup{};
	std::memcpy( &pStateBackup, pState, sizeof( CCSGOPlayerAnimState ) );

	// backup animation records
	LagRecord_t backup;
	backup.SetupRecord( pEntity, true );

	// setup traceray
	CGameTrace tr;

	auto ForcePlayerAngle = [&] ( float flAngle ) {
		pEntity->m_angEyeAngles( ).y = flAngle;
		pState->m_flEyeYaw = flAngle;
		pEntity->SetAbsAngles( QAngle( 0.f, flAngle, 0.f ) );
	};

	c_bone_builder pServerBones;

	const Vector vecBloodSpawn = pBlood->m_EffectData.m_vOrigin;
	const Vector vecBloodDirection = pBlood->m_EffectData.m_vNormal;

	const Vector vecTraceStart = vecBloodSpawn + ( vecBloodDirection * 64.0f );
	const Vector vecTraceExcld = vecBloodSpawn + ( vecBloodDirection * 1.f );

	CTraceFilterTargetSpecificEntity  filter;
	filter.m_iCollisionGroup = COLLISION_GROUP_NONE;
	filter.m_pTarget = pEntity;

	// 	g_pDebugOverlay->AddTextOverlay( vecBloodSpawn, 5.f, "%i", pBlood->m_EffectData.m_nHitBox );
	std::vector<float> flAvgAngles;
	std::vector<float> flRealAngles;

	float flBackupBodyYaw = pEntity->m_flPoseParameter( )[ 11 ];

	// step through each possible angle (hacky massive loop, purely for body_yaw)
	for( float _flAngle = 0.f; _flAngle <= 1080.f; _flAngle += 3.f ) {
		float body_yaw = _flAngle <= 360.0f ? 0.0f : _flAngle <= 720.0f ? -1.0f : 1.0f;
		pEntity->m_flPoseParameter( )[ 11 ] = body_yaw;

		float flAngle = Math::AngleNormalize( _flAngle / 4.f );

		// rotate the player
		ForcePlayerAngle( flAngle );

		// update animations for the player
		g_Animations.UpdatePlayerSimple( pEntity );

		// generate a rotated matrix
		alignas( 16 ) matrix3x4_t pRotatedMatrix[ MAXSTUDIOBONES ];
		pServerBones.store( pEntity, pRotatedMatrix, BONE_USED_BY_ANYTHING );
		pServerBones.setup( );

		// copy over our rotated bone matrix
		std::memcpy( pEntity->m_CachedBoneData( ).Base( ), pRotatedMatrix,
					 pEntity->m_CachedBoneData( ).Count( ) * sizeof( matrix3x4_t ) );

		// force the bone cache (so we can trace)
		pEntity->ForceBoneCache( );

		// shoot a trace coming from the direction of the blood splatter towards the spawn of the blood
		g_pEngineTrace->ClipRayToEntity( Ray_t( vecTraceStart, vecBloodSpawn ), MASK_SHOT, pEntity, &tr );

		if( tr.hit_entity && tr.hitgroup == nMatchedHitgroup ) {
			flAvgAngles.push_back( flAngle );

			g_pEngineTrace->ClipRayToEntity( Ray_t( vecTraceStart, vecTraceExcld ), MASK_SHOT, pEntity, &tr );

			if( !tr.hit_entity || tr.hitgroup != nMatchedHitgroup ) {
				flRealAngles.push_back( flAngle );
			}
		}
	}

	pEntity->m_flPoseParameter( )[ 11 ] = flBackupBodyYaw;

	float flRealAngle = 0.f;
	{
		for( auto &ang : flRealAngles ) {
			flRealAngle += ang;
		}

		flRealAngle /= ( float )flRealAngles.size( );

		if( !flRealAngles.empty( ) ) {
			EConfidence bloodConfidence = EConfidence::CONF_LOW;

			// ridiculous amount of data, ignore if its bigger
			// cos something definitely went wrong
			if( flAvgAngles.size( ) < 180 ) {
				// our real angle sample size is nice and small
				if( flRealAngles.size( ) <= 8 ) {
					// let's add this angle as a potentially accurate angle
					ResolverData_t::BloodAngle_t bloodAngle;
					bloodAngle.m_eConfidence = EConfidence::CONF_HIGH;
					bloodAngle.m_flTime = pEntity->m_flOldSimulationTime( ) + g_pGlobalVars->interval_per_tick;
					bloodAngle.m_flAngle = flRealAngle;

					data.m_vecBloodAngles.push_back( bloodAngle );
				}
				// okay it's bigger than 8
				// let's further look
				else {
					// smaller than 30, but the avg
					// sample size isn't TOO big
					if( flRealAngles.size( ) <= 30 &&
						flAvgAngles.size( ) <= 90 ) {
						ResolverData_t::BloodAngle_t bloodAngle;
						bloodAngle.m_eConfidence = flRealAngles.size( ) < 15 ? EConfidence::CONF_MED : EConfidence::CONF_LOW;
						bloodAngle.m_flTime = pEntity->m_flOldSimulationTime( ) + g_pGlobalVars->interval_per_tick;
						bloodAngle.m_flAngle = flRealAngle;

						data.m_vecBloodAngles.push_back( bloodAngle );
					}
				}
			}
		}
	}

	// restore back to previous anims
	backup.ApplyRecord( pEntity );
	std::memcpy( pState, &pStateBackup, sizeof( CCSGOPlayerAnimState ) );
}

void Resolver::OnBulletImpact( LagRecord_t *pRecord, ImpactInfo_t *info ) {
	auto pLocal = C_CSPlayer::GetLocalPlayer( );
	if( !pLocal )
		return;

	CCSGOPlayerAnimState *pState = pRecord->m_pEntity->m_PlayerAnimState( );
	if( !pState )
		return;

	if( !info )
		return;

	if( pRecord->m_eResolverStage != EResolverStages::RES_STAND )
		return;

	auto &data = GetResolverData( pRecord->m_pEntity->EntIndex( ) );

	// we can first push this angle back, since we missed on it anyway
	if( !info->m_bDealtDamage ) {
		ResolverData_t::DeductedAngle_t ang;
		ang.m_flAngle = Math::AngleNormalize( pRecord->m_angEyeAngles.y );
		ang.m_flTime = pRecord->m_flAnimationTime;

		data.m_vecDeductedAngles.push_back( ang );
	}

	CCSGOPlayerAnimState pStateBackup{};
	std::memcpy( &pStateBackup, pState, sizeof( CCSGOPlayerAnimState ) );

	// backup animation records
	LagRecord_t backup;
	backup.SetupRecord( pRecord->m_pEntity, true );

	// setup traceray
	CGameTrace tr;

	auto ForcePlayerAngle = [&] ( float flAngle ) {
		pRecord->m_pEntity->m_angEyeAngles( ).y = flAngle;
		pState->m_flEyeYaw = flAngle;
		pRecord->m_pEntity->SetAbsAngles( QAngle( 0.f, flAngle, 0.f ) );
	};

	// step through each possible angle
	for( float flAngle = -180.f; flAngle <= 180.f; flAngle += 1.f ) {
		bool bAlreadyExists = false;
		for( auto &deducted : data.m_vecDeductedAngles ) {
			if( deducted.m_flAngle == flAngle ) {
				bAlreadyExists = true;
				break;
			}
		}

		// let's not add duplicate angles
		if( bAlreadyExists )
			continue;

		// rotate the player
		ForcePlayerAngle( flAngle );

		// update animations for the player
		g_Animations.UpdatePlayerSimple( pRecord->m_pEntity );

		// rotate the player AGAIN for safety
		ForcePlayerAngle( flAngle );

		// generate a rotated matrix
		alignas( 16 ) matrix3x4_t pRotatedMatrix[ MAXSTUDIOBONES ];
		pRecord->m_sAnims[ ESides::SIDE_SERVER ].m_pBoneBuilder.store( pRecord->m_pEntity, pRotatedMatrix, BONE_USED_BY_ANYTHING );
		pRecord->m_sAnims[ ESides::SIDE_SERVER ].m_pBoneBuilder.setup( );

		// copy over our rotated bone matrix
		std::memcpy( pRecord->m_pEntity->m_CachedBoneData( ).Base( ), pRotatedMatrix,
					 pRecord->m_pEntity->m_CachedBoneData( ).Count( ) * sizeof( matrix3x4_t ) );

		// force the bone cache (so we can trace)
		pRecord->m_pEntity->ForceBoneCache( );

		// shoot a trace
		g_pEngineTrace->ClipRayToEntity( Ray_t( info->m_vecStart, info->m_vecEnd ), MASK_SHOT, pRecord->m_pEntity, &tr );

		// epic fail
		if( tr.hit_entity != pRecord->m_pEntity )
			continue;

		// we actually dealt damage to the player, but
		// not on the hitgroup we expected to hit.
		// let's see what angles he could've been at then
		if( info->m_bDealtDamage ) {
			// let's say our aimbot fired at the head hitgroup.
			// but with our info, we KNOW that we didn't hit head,
			// and instead we hit the stomach (or anywhere else).

			// at this angle, if the trace DID hit head hitbox 
			// like we expected to, then we can exclude this angle
			// because we know that our cheat didn't hit the expected
			// hitgroup, meaning that his head wasn't here.

			// note - michal;
			// TODO: account for spread!!!! this can definitely fail
			// if we're missing the expected hitgroup due to spread.
			// one way to do this is to compare the location of the 
			// client impact to that of the matched bullet impact,
			// if they're too far away from each other we can then
			// assume that it was a hitgroup mismatch due to spread !

			if( tr.hitgroup != info->m_iExpectedHitgroup )
				continue;
		}

		// this shot WOULD have intersected the player and dealt damage
		// but at this angle he would have been misresolved, therefore
		// we can exclude this angle from the possible angles we can bruteforce
		ResolverData_t::DeductedAngle_t ang;
		ang.m_flAngle = Math::AngleNormalize( flAngle );
		ang.m_flTime = pRecord->m_flAnimationTime;

		data.m_vecDeductedAngles.push_back( ang );
	}

	// restore back to previous anims
	backup.ApplyRecord( pRecord->m_pEntity );
	std::memcpy( pState, &pStateBackup, sizeof( CCSGOPlayerAnimState ) );
}

float Resolver::GetRelativePitch( LagRecord_t *pRecord, float flAngle ) {
	auto pLocal = C_CSPlayer::GetLocalPlayer( );
	if( pLocal->IsDead( ) )
		return flAngle;

	Vector angRelative;
	Math::VectorAngles( pLocal->m_vecOrigin( ) - pRecord->m_vecPredOrigin, angRelative );

	return Math::AngleNormalize( angRelative.x + flAngle );
}

float Resolver::GetRelativeYaw( LagRecord_t *pRecord, float flAngle ) {
	auto pLocal = C_CSPlayer::GetLocalPlayer( );
	if( pLocal->IsDead( ) )
		return flAngle;

	Vector angRelative;
	Math::VectorAngles( pLocal->m_vecOrigin( ) - pRecord->m_vecPredOrigin, angRelative );

	return Math::AngleNormalize( angRelative.y + flAngle );
}

float Resolver::GetFreestandYaw( LagRecord_t *pRecord ) {
	auto pLocal = C_CSPlayer::GetLocalPlayer( );
	if( !pLocal )
		return GetRelativeYaw( pRecord, 180.f );

	Vector angAway;
	Math::VectorAngles( pLocal->m_vecOrigin( ) - pRecord->m_vecPredOrigin, angAway );

	auto enemy_eyepos = pRecord->m_pEntity->GetEyePosition( );

	// construct vector of angles to test.
	std::vector< AdaptiveAngle > angles{ };
	angles.emplace_back( angAway.y + 180.f );
	angles.emplace_back( angAway.y + 90.f );
	angles.emplace_back( angAway.y - 90.f );

	// start the trace at the enemy shoot pos.
	auto start = pLocal->GetEyePosition( );

	// see if we got any valid result.
	// if this is false the path was not obstructed with anything.
	bool valid{ false };

	// iterate vector of angles.
	for( auto it = angles.begin( ); it != angles.end( ); ++it ) {

		// compute the 'rough' estimation of where our head will be.
		Vector end{ enemy_eyepos.x + std::cos( DEG2RAD( it->m_yaw ) ) * 32.f,
			enemy_eyepos.y + std::sin( DEG2RAD( it->m_yaw ) ) * 32.f,
			enemy_eyepos.z };

		// compute the direction.
		Vector dir = end - start;
		float len = dir.Normalize( );

		// should never happen.
		if( len <= 0.f )
			continue;

		// step thru the total distance, 4 units per step.
		for( float i{ 0.f }; i < len; i += 4.f ) {
			// get the current step position.
			Vector point = start + ( dir * i );

			// get the contents at this point.
			int contents = g_pEngineTrace->GetPointContents( point, MASK_SHOT_HULL );

			// contains nothing that can stop a bullet.
			if( !( contents & MASK_SHOT_HULL ) )
				continue;

			// append 'penetrated distance'.
			it->m_dist += ( 4.f * g_AntiAim.UpdateFreestandPriority( len, i, true ) );

			// mark that we found anything.
			valid = true;
		}
	}

	if( !valid ) {
		return Math::AngleNormalize( angAway.y + 180.f );
	}

	// put the most distance at the front of the container.
	std::sort( angles.begin( ), angles.end( ),
			   [ ] ( const AdaptiveAngle &a, const AdaptiveAngle &b ) {
		return a.m_dist > b.m_dist;
	} );

	// the best angle should be at the front now.
	AdaptiveAngle *best = &angles.front( );

	return Math::AngleNormalize( best->m_yaw );
}

EResolverStages Resolver::UpdateResolverStage( LagRecord_t *pRecord ) {
	C_CSPlayer *pLocal = C_CSPlayer::GetLocalPlayer( );
	if( !pLocal )
		return EResolverStages::RES_NONE;

	if( !pRecord )
		return EResolverStages::RES_NONE;

	QAngle angViewAngle;
	g_pEngine->GetViewAngles( angViewAngle );

	const float flFoV = Math::GetFov( angViewAngle, pLocal->GetEyePosition( ), pRecord->m_vecOrigin );
	const bool bOnGround = pRecord->m_fPredFlags & FL_ONGROUND;

	// we want to override, and they are on ground and are in close fov
	if( g_Vars.rage.resolver_override_key.enabled && bOnGround && flFoV <= 25 )
		return EResolverStages::RES_OVERRIDE;

	// player is moving & on ground
	if( pRecord->m_vecPredVelocity.Length2D( ) > 0.1f && bOnGround )
		return EResolverStages::RES_MOVE;

	// player is not on ground
	if( !bOnGround )
		return EResolverStages::RES_AIR;

	// player is standing still
	return EResolverStages::RES_STAND;
}

void Resolver::GetApproximateBodyState( LagRecord_t *pRecord, LagRecord_t *pPrevious ) {
	// get data, and reset missed shots on other stages
	auto &data = GetResolverData( pRecord->m_pEntity->EntIndex( ) );

	// assume not breaking lby, then try to detect
	// if they are breaking lby.
	data.m_eBodyState = EBodyState::BODY_DEFAULT;

	// can't do any further calcs...
	if( !pPrevious ) {
		return;
	}

	const auto pCurLayer = &pRecord->m_sAnims[ ESides::SIDE_SERVER ].m_pServerAnimOverlays[ 3 ];
	const auto pPrevLayer = &pPrevious->m_sAnims[ ESides::SIDE_SERVER ].m_pServerAnimOverlays[ 3 ];

	// 3 = ANIMATION_LAYER_ADJUST
	const int nCurSeq = pRecord->m_pEntity->GetSequenceActivity( pCurLayer->m_nSequence );

#if 0
	std::vector<std::string> vecInfo;

	char buffer[ 128 ] = {};

	sprintf( buffer, "current sequence: %d", nCurSeq );
	vecInfo.push_back( buffer );

	sprintf( buffer, "current layer weight: %.6f", pCurLayer->m_flWeight );
	vecInfo.push_back( buffer );

	sprintf( buffer, "current layer cycle: %.6f", pCurLayer->m_flCycle );
	vecInfo.push_back( buffer );

	sprintf( buffer, "current layer playback: %.6f", pCurLayer->m_flPlaybackRate );
	vecInfo.push_back( buffer );

	sprintf( buffer, "previous layer weight: %.6f", pPrevLayer->m_flWeight );
	vecInfo.push_back( buffer );

	sprintf( buffer, "previous layer cycle: %.6f", pPrevLayer->m_flCycle );
	vecInfo.push_back( buffer );

	sprintf( buffer, "previous layer playback: %.6f", pPrevLayer->m_flPlaybackRate );
	vecInfo.push_back( buffer );

	sprintf( buffer, "current layer lby: %.1f", pRecord->m_flLowerBodyYawTarget );
	vecInfo.push_back( buffer );

	sprintf( buffer, "current stand_idle: %.6f", pRecord->m_pEntity->m_flPoseParameter( )[ AIM_BLEND_STAND_IDLE ] );
	vecInfo.push_back( buffer );

	g_Vars.globals.vecLayerDebug = vecInfo;
#endif

	// start checking thru animlayers

	// 980 after stopped moving:
	// means hes breaking lby to right side OR hes not breaking lby
	// in this case, we check if the last sequence was 980 and if his lby updated
	// if so then we can assume hes breaking lby.

	// 979 after stopped moving:
	// breaking left side, if he constantly keeps triggering 979
	// then we can assume hes breaking lby too (z break or >120 delta break)

	// https://streamable.com/27clm3

	// layer isn't currently updating
	// this either means they're not breaking lby, breaking under 120 delta, or supressing lby break (unlikely)
	const bool bInactiveLayer = pCurLayer->m_flWeight == 0.f && pCurLayer->m_flWeight == pPrevLayer->m_flWeight;
	if( bInactiveLayer ) {
		data.m_bAdjusting = false;

		// since this is a little harder to detect,
		// let's just assume breaking here until
		// we can definitely decide they're NOT breaking lby
		data.m_eBodyState = EBodyState::BODY_BREAK;

		// in this case, both 979 and 980 will suffice
		// as if the player is breaking to the right side
		// and they stop moving 979 won't play and instead 
		// will stay on 980 (unless their angle changes)
		// https://streamable.com/h3i6mg

		// todo

		// no valid last safe body to refer to
		// let's refer to some other guesswork

		// let's compare lby against both freestand angle and last move angle
		// i don't want to JUST check last moving body here cos they could
		// be breaking last move lby, and it could fail that way too.
		// if this sucks, maybe check for edge angle too, just for more safety
		if( data.IsValidFloat( data.m_flLastMovingBody ) &&
			data.IsValidFloat( data.m_flFreestandYaw ) ) {
			// last moving lby and freestand angle are really close
			if( fabsf( Math::AngleDiff( data.m_flLastMovingBody, data.m_flFreestandYaw ) ) < 25.f ) {
				// freestand angle or last move lby are close to the lby
				if( fabsf( Math::AngleDiff( pRecord->m_flLowerBodyYawTarget, data.m_flFreestandYaw ) ) < 25.f ||
					fabsf( Math::AngleDiff( pRecord->m_flLowerBodyYawTarget, data.m_flLastMovingBody ) ) < 25.f ) {
					// let's assume they're not breaking lby then !!!
					data.m_eBodyState = EBodyState::BODY_DEFAULT;
				}
			}
		}

		// we are fully done here :D
		return;
	}

	// layer isn't inactive, let's check if 
	// they're breaking lby!! we only care about
	// 979 here, since 980 is only important when they
	// are breaking under 120. when you stop moving and 
	// you're breaking with 120+ delta, 979 WON'T get triggered
	// for the first flick, but for the rest of the flicks it will.
	if( nCurSeq != 979 ) {
		// 979 anim layer and u dump it dude how?
		return;
	}

	// weight is either about to increment or is incrementing
	if( pCurLayer->m_flWeight == 1.f || pCurLayer->m_flWeight != pPrevLayer->m_flWeight ) {
		data.m_flLastWeightAdjustTime = pRecord->m_flAnimationTime;
	}

	// can't do anything yet
	if( !data.IsValidFloat( data.m_flLastWeightAdjustTime ) ) {
		return;
	}

	// check if the last weight increment is consistent with how often
	// the layers should update (layers update per player simulation time update)
	if( TIME_TO_TICKS( pRecord->m_flAnimationTime - data.m_flLastWeightAdjustTime ) > pRecord->m_nChokedTicks + 1 ) {
		// too long has passed.
		return;
	}

	// the cycle is also currently incrementing
	if( pCurLayer->m_flCycle != pPrevLayer->m_flCycle ) {
		data.m_flLastCycleAdjustTime = pRecord->m_flAnimationTime;
	}

	// can't do anything yet
	if( !data.IsValidFloat( data.m_flLastCycleAdjustTime ) ) {
		return;
	}

	// check if the last cycle increment is consistent with how often
	// the layers should update (layers update per player simulation time update)
	if( TIME_TO_TICKS( pRecord->m_flAnimationTime - data.m_flLastCycleAdjustTime ) > pRecord->m_nChokedTicks + 1 ) {
		// too long has passed.
		return;
	}

	// yep, 979/balance adjust keeps playing/restarting. this must mean
	// that they're either fail breaking (to the left, cos 979 doesn't
	// play when you fail break to the right side), or they're breaking
	// lby consistently over 120 delta OR they're using some pre-flick lby breaker.
	data.m_eBodyState = EBodyState::BODY_BREAK;
	data.m_bAdjusting = true;

	// could use this to update lby flick timer
	// not sure if to use simtime here or animtime
	// const float flAnimationStartTime = pRecord->m_flSimulationTime - pCurLayer->m_flCycle;
}

void Resolver::GetApproximateBodyDelta( LagRecord_t *pRecord, LagRecord_t *pPrevious ) {
	auto &data = GetResolverData( pRecord->m_pEntity->EntIndex( ) );
	if( !pPrevious ) {
		return;
	}

	data.m_flLastSafeBody = -1.f;

	if( pRecord->m_vecPredVelocity.Length2D( ) > 0.1f ) {
		std::memcpy( &data.m_pMoveData, pRecord, sizeof( LagRecord_t ) );
		return;
	}

	auto pMoveRecord = &data.m_pMoveData;
	if( !pMoveRecord ) {
		return;
	}

	if( pMoveRecord->m_vecOrigin.DistanceSquared( pRecord->m_vecOrigin ) > 32.f || pMoveRecord->m_flAnimationTime <= 0.f ) {
		return;
	}

	if( pRecord->m_flAnimationTime - pMoveRecord->m_flAnimationTime < 0.22f ) {
		data.m_flLastSafeBody = pRecord->m_flLowerBodyYawTarget;
	}

	data.m_flApproxDelta = Math::AngleDiff( pRecord->m_flLowerBodyYawTarget, data.m_flLastSafeBody );
}

void Resolver::OnPlayerStand( LagRecord_t *pRecord, LagRecord_t *pPrevious ) {
	// get data, and reset missed shots on other stages
	auto &data = GetResolverData( pRecord->m_pEntity->EntIndex( ) );
	data.UpdateMissedShots( EResolverStages::RES_STAND );

	if( pPrevious ) {
		// lby updated here, we got them resolved d-_-b
		if( pRecord->m_flLowerBodyYawTarget != pPrevious->m_flLowerBodyYawTarget ) {
			pRecord->m_iResolverType = 8;
			pRecord->m_szResolver = XorStr( "lby update" );
			goto FORCE_BODY;
		}
	}

	// lby SHOULD have updated here based on our timer, tap lby flick!
	if( pRecord->m_flAnimationTime >= data.m_flNextBodyUpdate ) {
		pRecord->m_iResolverType = 9;
		pRecord->m_szResolver = XorStr( "lby flick" );

	FORCE_BODY:
		pRecord->m_angEyeAngles.y = pRecord->m_flLowerBodyYawTarget;

		// update our timer again :)
		data.m_flNextBodyUpdate = pRecord->m_flAnimationTime + 1.1f;

		// notify our cheat that we're trying to shoot lby flick !
		data.m_bPredictingBody = true;

		// mark this record as lby flick
		pRecord->m_bLBYFlicked = true;

		// they're resolved here
		pRecord->m_bIsResolved = true;

		// update foot yaw direction
		pRecord->m_pEntity->SetAbsAngles( QAngle( 0.f, pRecord->m_flLowerBodyYawTarget, 0.f ) );

		pRecord->m_eConfidence = EConfidence::CONF_VHIGH;
		return;
	}

	pRecord->m_iResolverType = 10;
	pRecord->m_szResolver = XorStr( "base away" );

	// not sure if they're resolved yet, we shall see
	pRecord->m_bIsResolved = false;

	// clear previous angles before
	// we do anything
	data.m_vecAngles.clear( );

	// adds a bruteforce angle rounded up
	auto AddAngle = [&] ( float flAngle ) -> void {
		data.m_vecAngles.push_back( std::roundf( Math::AngleNormalize( flAngle ) ) );
	};

	// this will be the angle we're most confident in.
	// start at away angle, this will get overwritten anyway
	const float flRelativeAngle = GetRelativeYaw( pRecord, 180.f );
	float flIdealAngle = flRelativeAngle;

	// low confidence to start with
	data.m_eIdealConfidence = EConfidence::CONF_LOW;

	// major difference between freestand angle and current confidence angle
	// this means the freestand angle is probably +- 90
	if( data.IsValidFloat( data.m_flFreestandYaw ) && fabsf( Math::AngleDiff( flIdealAngle, data.m_flFreestandYaw ) ) > 5.f ) {
		bool bInvalidAngle = !data.IsValidFloat( data.m_flEdgeYaw );
		// we previously missed this guy by shooting at his freestand angle
		// let's only allow this stage if it 'verifies' the angle by comparing to the edge angle
		if( data.m_bMissedFreestand && !bInvalidAngle ) {
			// too far away bro, don't let it get used
			if( fabsf( Math::AngleDiff( data.m_flEdgeYaw, data.m_flFreestandYaw ) ) > 10.f ) {
				bInvalidAngle = true;
			}
		}

		if( !bInvalidAngle ) {
			flIdealAngle = data.m_flFreestandYaw;

			// medium confidence, they don't have to be freestanding
			data.m_eIdealConfidence = EConfidence::CONF_MED;

			pRecord->m_iResolverType = 12;
			pRecord->m_szResolver = XorStr( "freestand" );
		}
	}

	float flSafeBodyDelta = data.m_flApproxDelta;
	if( data.m_bAdjusting ) {
		flSafeBodyDelta = std::clamp( flSafeBodyDelta, -180.f, 180.f );
	}
	else {
		flSafeBodyDelta = std::clamp( flSafeBodyDelta, -120.f, 120.f );
	}

	// estimate where their real would be 
	if( data.IsValidFloat( data.m_flApproxDelta ) && fabsf( flSafeBodyDelta ) > 25.f ) {
		flIdealAngle = pRecord->m_flLowerBodyYawTarget + flSafeBodyDelta;

		// very high confidence, they haven't updated lby yet
		data.m_eIdealConfidence = EConfidence::CONF_MED;

		pRecord->m_iResolverType = 155;
		pRecord->m_szResolver = XorStr( "approx angle" );
	}

	// we saw them move recently, so let's try to shoot at the last moving body
	if( data.IsValidFloat( data.m_flLastMovingTime ) && fabsf( pRecord->m_flAnimationTime - data.m_flLastMovingTime ) < 3.5f ) {
		flIdealAngle = data.m_flLastMovingBody;

		// medium confidence, some cheats could be breaking last move lby
		data.m_eIdealConfidence = EConfidence::CONF_MED;

		pRecord->m_iResolverType = 11;
		pRecord->m_szResolver = XorStr( "last move" );
	}

	// not breaking lby, we can do lby here 
	if( data.m_eBodyState == EBodyState::BODY_DEFAULT ) {
		flIdealAngle = pRecord->m_flLowerBodyYawTarget;

		// high confidence, they're not breaking lby (but the check could fail)
		data.m_eIdealConfidence = EConfidence::CONF_HIGH;

		pRecord->m_iResolverType = 13;
		pRecord->m_szResolver = XorStr( "no fake" );
	}

	// they haven't had time to start breaking lby yet, their head will still be at lby
	if( data.IsValidFloat( data.m_flLastSafeBody ) && fabsf( pRecord->m_flAnimationTime - data.m_flLastMovingTime ) < 0.22f ) {
		flIdealAngle = data.m_flLastSafeBody;

		// very high confidence, they haven't updated lby yet
		data.m_eIdealConfidence = EConfidence::CONF_VHIGH;

		pRecord->m_iResolverType = 14;
		pRecord->m_szResolver = XorStr( "last safe body" );
	}

	// their last move is the same as their current lby, they haven't updated at all
	if( data.IsValidFloat( data.m_flLastMovingTime ) && data.m_flLastMovingBody == pRecord->m_flLowerBodyYawTarget ) {
		// we haven't previously missed this guy on this stage,
		// feel free to shoot at it 
		if( !data.IsValidInt( data.m_nMissedBody ) ) {
			flIdealAngle = data.m_flLastMovingBody;

			// very high confidence, they haven't updated lby yet
			data.m_eIdealConfidence = EConfidence::CONF_VHIGH;

			pRecord->m_iResolverType = 15;
			pRecord->m_szResolver = XorStr( "same lby" );
		}
		// yeah we missed him here before
		// let's add some safety for this angle
		else {
			// we missed him once on here before
			if( data.m_nMissedBody == 0 ) {
				// first check if freestand yaw is valid, if so compare it to this angle, if they're close then let's allow resolver to use it
				// then if the first freestand valid check fails, check for a valid edge angle and compare to that, if both fail we just don't use this at all
				if( ( data.IsValidFloat( data.m_flFreestandYaw ) && fabsf( Math::AngleDiff( data.m_flLastMovingBody, data.m_flFreestandYaw ) ) < 10.f ) ||
					( data.IsValidFloat( data.m_flEdgeYaw ) && fabsf( Math::AngleDiff( data.m_flLastMovingBody, data.m_flEdgeYaw ) ) < 10.f ) ) {
					flIdealAngle = data.m_flLastMovingBody;

					// high confidence, they haven't updated lby yet (but we missed once before)
					data.m_eIdealConfidence = EConfidence::CONF_HIGH;

					pRecord->m_iResolverType = 16;
					pRecord->m_szResolver = XorStr( "same lby" );
				}
			}
			else { /*if we miss him again after this point let's just not shoot at it ever again*/ }
		}
	}

	// we have valid blood angle data
	if( !data.m_vecBloodAngles.empty( ) ) {
		for( auto &bloodAngle : data.m_vecBloodAngles ) {
			// nah too old, let's not trust it
			if( fabsf( bloodAngle.m_flTime - pRecord->m_flAnimationTime ) > 4.5f )
				continue;

			// we already have an angle that we're more confident in
			if( data.m_eIdealConfidence >= bloodAngle.m_eConfidence ) {
				// low confidence anyway so why not try it lel
				if( data.m_eIdealConfidence != EConfidence::CONF_LOW )
					continue;
			}

			// oki we found our desired blood angle
			// let's tell our resolver to use it :D
			flIdealAngle = bloodAngle.m_flAngle;
			data.m_eIdealConfidence = bloodAngle.m_eConfidence;
			pRecord->m_iResolverType = 17;

			pRecord->m_szResolver = XorStr( "blood" );
			break;
		}
	}

	// we keep missing shots and we still haven't found a nicer ideal yaw (still at relative)
	if( auto nMissed = data.GetMissedShots( EResolverStages::RES_STAND );
		nMissed > 0 ) {
		// let's try and guess some other angles
		switch( ( nMissed - 1 ) % 3 ) {
			case 0:
				// just try fallback on freestand yaw 
				if( data.IsValidFloat( data.m_flFreestandYaw ) ) {
					// try lby, maybe they're not breaking
					flIdealAngle = data.m_flFreestandYaw;

					pRecord->m_iResolverType = 18;
					pRecord->m_szResolver = XorStr( "fallback auto" );
				}
				// bruh no good freestand angle somehow
				else {
					// flip this muderfucker
					flIdealAngle = pRecord->m_flLowerBodyYawTarget + 180.f;

					pRecord->m_iResolverType = 19;
					pRecord->m_szResolver = XorStr( "fallback flip" );
				}
				break;
			case 1:
				// try lby, maybe they're not breaking
				flIdealAngle = pRecord->m_flLowerBodyYawTarget;

				pRecord->m_iResolverType = 20;
				pRecord->m_szResolver = XorStr( "fallback lby" );
				break;
			case 2:
				const bool bUsedFreestandBefore = data.IsValidFloat( data.m_flFreestandYaw );
				// we used freestand yaw last stage, let's try flip it this time
				if( bUsedFreestandBefore ) {
					flIdealAngle = data.m_flFreestandYaw + 180.f;

					pRecord->m_iResolverType = 21;
					pRecord->m_szResolver = XorStr( "fallback opp auto" );
				}
				else {  // idk maybe they're facing us ?? xd
					flIdealAngle = GetRelativeYaw( pRecord, 0.f );

					pRecord->m_iResolverType = 22;
					pRecord->m_szResolver = XorStr( "fallback forward" );
				}
				break;
		}
	}

	const bool bHasValidAdjustWeight = data.IsValidFloat( data.m_flLastWeightAdjustTime );
	const bool bHasValidAdjustCycle = data.IsValidFloat( data.m_flLastCycleAdjustTime );

	const bool bRecentlyHasntAdjusted =
		( bHasValidAdjustWeight && fabsf( data.m_flLastWeightAdjustTime - pRecord->m_flAnimationTime ) > TICKS_TO_TIME( 7 ) ) ||
		( bHasValidAdjustCycle && fabsf( data.m_flLastCycleAdjustTime - pRecord->m_flAnimationTime ) > TICKS_TO_TIME( 7 ) );

	auto fnFillAngles = [&] ( ) {
		// fill the vector with all possible angles
		for( float flAngle = -180.f; flAngle <= 180.f; flAngle += 2.f ) {
			const float flRounded = std::roundf( Math::AngleNormalize( flAngle ) );
			bool bUnlikelyAngle = false;

			// if we have any deducted angles, try to see
			// if this current angle is close to it. if so,
			// let's exclude it from the angles we'll shoot at
			if( data.m_vecDeductedAngles.size( ) ) {
				for( auto &badAngle : data.m_vecDeductedAngles ) {
					if( fabsf( Math::AngleDiff( flRounded, badAngle.m_flAngle ) ) <= 20.f ) {
						bUnlikelyAngle = true;
						break;
					}
				}
			}

			// let's see if we can deduct any other angles
			if( bRecentlyHasntAdjusted || !bHasValidAdjustWeight || !bHasValidAdjustCycle ) {
				// the player hasn't been triggering 979 for a while.
				// this means that his head must be within 120 degrees of his lby
				if( fabsf( Math::AngleDiff( flRounded, pRecord->m_flLowerBodyYawTarget ) ) > 120.f ) {
					bUnlikelyAngle = true;
				}
			}

			// yep! don't add it 
			if( bUnlikelyAngle )
				continue;

			AddAngle( std::roundf( Math::AngleNormalize( flAngle ) ) );
		}
	};

	// make this a lambda so we can call it 
	// again later instead of using goto statements
	fnFillAngles( );

	// shit, we deducted all our angles, and now we have no angles left...
	// let's try and rationalise some angles and bring them back.
	if( data.m_vecAngles.empty( ) ) {
		// determine if we want this angle back
		auto fnBringBackAngle = [&] ( ResolverData_t::DeductedAngle_t uDeducted ) -> bool {
			// it's been 3 seconds, they definitely had time to change angles
			if( fabsf( uDeducted.m_flTime - pRecord->m_flAnimationTime ) > 3.f ) {
				// the confidence of the ideal angle is high or more, let's bring it back
				if( data.m_eIdealConfidence >= EConfidence::CONF_HIGH ) {
					if( fabsf( Math::AngleDiff( uDeducted.m_flAngle, flIdealAngle ) ) < 10.f ) {
						return true;
					}
				}
			}

			// player hasn't been triggerng 979 in a while
			if( bRecentlyHasntAdjusted || ( !bHasValidAdjustWeight || !bHasValidAdjustCycle ) ) {
				// this angle was 'deducted', but it's likely
				// that their head is within this range.
				if( fabsf( Math::AngleDiff( uDeducted.m_flAngle, pRecord->m_flLowerBodyYawTarget ) ) <= 120.f ) {
					// let's erase this angle from deducted angle list
					return true;
				}
			}

			// todo; more conditions
			return false;
		};

		// let's reset this here, if we literally ran 
		// out of angles on this guy we might aswell allow it 
		data.m_bMissedFreestand = false;

		// now let's erase some deducted angles
		data.m_vecDeductedAngles.erase(
			std::remove_if( data.m_vecDeductedAngles.begin( ), data.m_vecDeductedAngles.end( ), fnBringBackAngle ),
			data.m_vecDeductedAngles.end( ) );

		// okay, after erasing our deducted angles let's fill our 
		// angles gain and append our bruteforce angles.
		fnFillAngles( );
	}

	// g_Vars.globals.vecBrutePreSort = data.m_vecAngles;

	// now let's sort the final angles. we want to
	// sort them in order of those which are closest to
	// our confidence angle.
	std::sort( data.m_vecAngles.begin( ), data.m_vecAngles.end( ), [&] ( const float a, const float b ) -> bool {
		const float flDeltaFromA = fabsf( Math::AngleDiff( a, flIdealAngle ) );
		const float flDeltaFromB = fabsf( Math::AngleDiff( b, flIdealAngle ) );

		return flDeltaFromA < flDeltaFromB;
	} );

	// g_Vars.globals.vecBrutePostSort = data.m_vecAngles;


	// note - michal;
	// confidence resolver logic will go like this:
	// in the if statements below, we will determine our confidence angle
	// base on what it is, we will either set it as 'low', 'medium', 'high' confidence
	// of course, it can sometimes happen that our exact confidence angle (or close) is 'deducted'
	// therefor we will have another confidence system, to see how close the delta is between
	// our chosen confidence angle and the closest angle (after sorting deducted angles).
	// so say the angle is very far ('low' confidence), and our first confidence angle is considered 'high',
	// the final confidence we will have will be 'medium' - the average of the two.
	{ /* (...) */
		const float flPostSortIdealDelta = Math::AngleDiff( data.m_vecAngles.front( ), flIdealAngle );

		// our front angles after deduction are really close
		// to our ideal angle, very high confidence
		if( flPostSortIdealDelta <= 5.f ) {
			data.m_eAngleConfidence = EConfidence::CONF_VHIGH;
		}
		// our front angles after deduction are still kinda close
		// to our ideal angle, say high confidence here
		else if( flPostSortIdealDelta <= 15.f ) {
			data.m_eAngleConfidence = EConfidence::CONF_HIGH;
		}
		// our front angles after deduction are not really close
		// to our ideal angle, i guess just medium
		else if( flPostSortIdealDelta <= 45.f ) {
			data.m_eAngleConfidence = EConfidence::CONF_MED;
		}
		// our front angles after deduction are not more than 
		// 45 degrees away than our ideal one - not confident at all.
		else if( flPostSortIdealDelta > 45.f ) {
			data.m_eAngleConfidence = EConfidence::CONF_LOW;
		}

		// this can't be higher than our ideal confidence,
		// since this distance system is only used to scale down
		if( data.m_eAngleConfidence > data.m_eIdealConfidence )
			data.m_eAngleConfidence = data.m_eIdealConfidence;

		// let's get this record's confidence state, and make sure to round down
		pRecord->m_eConfidence = static_cast< EConfidence >( std::floor( ( data.m_eAngleConfidence + data.m_eIdealConfidence ) / 2 ) );
	}

	// finally, let's resolve the player.
	pRecord->m_angEyeAngles.y = data.m_vecAngles.front( );

	// determine if we can deem this record 'resolved'
	pRecord->m_bIsResolved = pRecord->m_eConfidence >= EConfidence::CONF_HIGH;
}

// will just make a "secondary" resolver here, just for testing and so people can still use the old if wanted
void Resolver::OnPlayerStandTrial( LagRecord_t *pRecord, LagRecord_t *pPrevious ) {
	// get data, and reset missed shots on other stages
	auto &data = GetResolverData( pRecord->m_pEntity->EntIndex( ) );
	data.UpdateMissedShots( EResolverStages::RES_STAND );

	if( pPrevious ) {
		// lby updated here, we got them resolved d-_-b
		if( pRecord->m_flLowerBodyYawTarget != pPrevious->m_flLowerBodyYawTarget ) {
			pRecord->m_iResolverType = 8;
			pRecord->m_szResolver = XorStr( "lby update" );

			// change is big enough to mark as an lby update
			if( fabs( Math::AngleDiff( pRecord->m_flLowerBodyYawTarget, pPrevious->m_flLowerBodyYawTarget ) ) > 35 )
				data.m_bHasUpdatedLBY = true;

			goto FORCE_BODY;
		}
	}

	// lby SHOULD have updated here based on our timer, tap lby flick!
	if( pRecord->m_flAnimationTime >= data.m_flNextBodyUpdate ) {
		pRecord->m_iResolverType = 9;
		pRecord->m_szResolver = XorStr( "lby flick" );

	FORCE_BODY:
		pRecord->m_angEyeAngles.y = pRecord->m_flLowerBodyYawTarget;

		// update our timer again :)
		data.m_flNextBodyUpdate = pRecord->m_flAnimationTime + 1.1f;

		// notify our cheat that we're trying to shoot lby flick !
		data.m_bPredictingBody = true;

		// mark this record as lby flick
		pRecord->m_bLBYFlicked = true;

		// they're resolved here
		pRecord->m_bIsResolved = true;

		// update foot yaw direction
		pRecord->m_pEntity->SetAbsAngles( QAngle( 0.f, pRecord->m_flLowerBodyYawTarget, 0.f ) );

		pRecord->m_eConfidence = EConfidence::CONF_VHIGH;
		return;
	}

	// not sure if they're resolved yet, we shall see
	pRecord->m_bIsResolved = false;

	// clear previous angles before
	// we do anything
	data.m_vecAngles.clear( );

	// adds a bruteforce angle rounded up
	auto AddAngle = [ & ]( float flAngle ) -> void {
		data.m_vecAngles.push_back( std::roundf( Math::AngleNormalize( flAngle ) ) );
	};

	auto fnFillAngles = [ & ]( ) {
		// fill the vector with all possible angles
		for( float flAngle = -180.f; flAngle <= 180.f; flAngle += 2.f ) {
			const float flRounded = std::roundf( Math::AngleNormalize( flAngle ) );
			bool bUnlikelyAngle = false;

			// if we have any deducted angles, try to see
			// if this current angle is close to it. if so,
			// let's exclude it from the angles we'll shoot at
			if( data.m_vecDeductedAngles.size( ) ) {
				for( auto &badAngle : data.m_vecDeductedAngles ) {
					if( fabsf( Math::AngleDiff( flRounded, badAngle.m_flAngle ) ) <= 20.f ) {
						bUnlikelyAngle = true;
						break;
					}
				}
			}

			// yep! don't add it 
			if( bUnlikelyAngle )
				continue;

			AddAngle( std::roundf( Math::AngleNormalize( flAngle ) ) );
		}
	};

	// make this a lambda so we can call it 
	// again later instead of using goto statements
	// we are going to be using this for bruteforce
	fnFillAngles( );

	// get away angle
	const float flRelativeAngle = GetRelativeYaw( pRecord, 180.f );

	// lambda for setting resolver stage
	auto SetResolverStage = [ ]( LagRecord_t *pRecord, ResolverData_t data, float flAngle, EConfidence eConfidence, int nType, std::string szString ) {
		pRecord->m_angEyeAngles.y = flAngle;

		pRecord->m_eConfidence = eConfidence;

		pRecord->m_iResolverType = nType;
		pRecord->m_szResolver = szString;
	};

	// create variable we will be using.
	float flCompareAngle = pRecord->m_flLowerBodyYawTarget;

	// if body yaw updated it means body yaw is no longer valid to do calculations with
	// so if we have a valid moving yaw, use this for calculations instead
	if( data.IsValidFloat( data.m_flLastMovingTime ) && data.m_bHasUpdatedLBY ) {
		// prefer using last move instead of current lby
		flCompareAngle = data.m_flLastMovingBody;
	}

	int nMissed = data.GetMissedShots( EResolverStages::RES_STAND );

	// haven't missed a shot yet, we can try resolving
	if( nMissed <= 0 ) {
		// start at base angle
		SetResolverStage( pRecord, data, flRelativeAngle, EConfidence::CONF_LOW, 10, XorStr( "base away" ) );

		// they haven't had time to start breaking lby yet, their head will still be at lby
		if( data.IsValidFloat( data.m_flLastSafeBody ) && fabsf( pRecord->m_flAnimationTime - data.m_flLastMovingTime ) < 0.22f ) {
			SetResolverStage( pRecord, data, data.m_flLastSafeBody, EConfidence::CONF_VHIGH, 14, XorStr( "last safe body" ) );

			// exit out
			return;
		}

		// lby and edge angle are close enough, use this
		if( data.IsValidFloat( data.m_flEdgeYaw ) && fabsf( data.m_flEdgeYaw - pRecord->m_flLowerBodyYawTarget ) <= 36 ) {
			SetResolverStage( pRecord, data, data.m_flEdgeYaw, EConfidence::CONF_HIGH, 11, XorStr( "edge" ) );

			// exit out
			return;
		}

		// not breaking lby, we can do lby here
		if( data.m_eBodyState == EBodyState::BODY_DEFAULT ) {
			SetResolverStage( pRecord, data, pRecord->m_flLowerBodyYawTarget, EConfidence::CONF_HIGH, 13, XorStr( "no fake" ) );
		}

		// we saw them move recently, so let's try to shoot at the last moving body
		else if( data.IsValidFloat( data.m_flLastMovingTime ) && fabsf( pRecord->m_flAnimationTime - data.m_flLastMovingTime ) <= 3.f && fabsf( Math::AngleDiff( flCompareAngle, data.m_flLastMovingBody ) <= 30.f ) ) {
			SetResolverStage( pRecord, data, data.m_flLastMovingBody, EConfidence::CONF_MED, 11, XorStr( "last move" ) );
		}

		// estimate where their real would be 
		//else if( data.IsValidFloat( data.m_flApproxDelta ) && fabsf( data.m_flApproxDelta ) >= 25.f ) {
		//	SetResolverStage( pRecord, data, pRecord->m_flLowerBodyYawTarget + data.m_flApproxDelta, EConfidence::CONF_MED, 155, XorStr( "approx angle" ) );
		//}

		// overlap with back is close
		else if( fabsf( Math::AngleDiff( flCompareAngle, flRelativeAngle ) ) <= 15.f ) {
			SetResolverStage( pRecord, data, flRelativeAngle, EConfidence::CONF_MED, 193, XorStr( "back" ) );
		}

		// overlap with forward is close
		else if( fabsf( Math::AngleDiff( flCompareAngle, flRelativeAngle + 180.f ) ) <= 15.f && !data.m_bHasUpdatedLBY ) {
			SetResolverStage( pRecord, data, flRelativeAngle + 180.f, EConfidence::CONF_MED, 194, XorStr( "forward" ) );
		}
	}

	// brute force
	else {
		if( g_Vars.rage.deduct_bruteforce ) {
			float flTargetYaw = flRelativeAngle;
			float flSmallestAngleDiff = 1000.f;
			for( auto angle : data.m_vecAngles ) {
				float flAngleDiff = Math::AngleDiff( angle, flRelativeAngle );

				if( flAngleDiff < flSmallestAngleDiff ) {
					flSmallestAngleDiff = flAngleDiff;
					flTargetYaw = angle;
				}
			}

			SetResolverStage( pRecord, data, flTargetYaw, EConfidence::CONF_LOW, 78, std::string( XorStr( "brute angle: " ) ).append( std::to_string( flTargetYaw ) ) );
		}
		else {
			// let's try and guess some other angles
			switch( ( nMissed - 1 ) % 8 ) {
				case 0:
					SetResolverStage( pRecord, data, flRelativeAngle, EConfidence::CONF_LOW, 78, XorStr( "fallback base away" ) );
					break;
				case 1:
					SetResolverStage( pRecord, data, flRelativeAngle - 45.f, EConfidence::CONF_LOW, 78, XorStr( "fallback base away -" ) );
					break;
				case 2:
					SetResolverStage( pRecord, data, flRelativeAngle + 45.f, EConfidence::CONF_LOW, 78, XorStr( "fallback base away +" ) );
					break;
				case 3:
					SetResolverStage( pRecord, data, flRelativeAngle + 90.f, EConfidence::CONF_LOW, 78, XorStr( "fallback base away ++" ) );
					break;
				case 4:
					SetResolverStage( pRecord, data, flRelativeAngle - 90.f, EConfidence::CONF_LOW, 78, XorStr( "fallback base away --" ) );
					break;
				case 5:
					SetResolverStage( pRecord, data, flRelativeAngle + 180.f, EConfidence::CONF_LOW, 78, XorStr( "fallback base away+++" ) );
					break;
				case 6:
					SetResolverStage( pRecord, data, pRecord->m_flLowerBodyYawTarget, EConfidence::CONF_LOW, 78, XorStr( "fallback lower body" ) );
					break;
				case 7:
					SetResolverStage( pRecord, data, data.m_flLastSafeBody, EConfidence::CONF_LOW, 78, XorStr( "fallback last safe body" ) );
					break;
			}
		}
	}

	// shit, we deducted all our angles, and now we have no angles left...
	// let's try and rationalise some angles and bring them back.
	if( data.m_vecAngles.empty( ) ) {
		// determine if we want this angle back
		auto fnBringBackAngle = [ & ]( ResolverData_t::DeductedAngle_t uDeducted ) -> bool {
			// return all of them back for now, change this up later..
			return true;
		};

		// now let's erase some deducted angles
		data.m_vecDeductedAngles.erase(
			std::remove_if( data.m_vecDeductedAngles.begin( ), data.m_vecDeductedAngles.end( ), fnBringBackAngle ),
			data.m_vecDeductedAngles.end( ) );

		// okay, after erasing our deducted angles let's fill our 
		// angles gain and append our bruteforce angles.
		fnFillAngles( );
		// printf( "filling back deducted angles\n" );
	}

	// determine if we can deem this record 'resolved'
	pRecord->m_bIsResolved = pRecord->m_eConfidence >= EConfidence::CONF_HIGH;
}

void Resolver::OnPlayerMove( LagRecord_t *pRecord, LagRecord_t *pPrevious ) {
	// get data, and reset missed shots on other stages
	auto &data = GetResolverData( pRecord->m_pEntity->EntIndex( ) );
	data.UpdateMissedShots( EResolverStages::RES_MOVE );

	pRecord->m_iResolverType = 7;
	pRecord->m_szResolver = XorStr( "move" );

	// Resolve The Player : D
	pRecord->m_angEyeAngles.y = pRecord->m_flLowerBodyYawTarget;

	// save data lel
	data.m_flLastMovingBody = pRecord->m_flLowerBodyYawTarget;
	data.m_flLastMovingTime = pRecord->m_flAnimationTime;

	// note - michal;
	// idk if we should do this for LBY prediction
	// we can probably save it to some variable, which we can
	// later use to determine if they've done their first lby flick
	// but since our velocity isnt PERFECT this + 0.22f could cause
	// mispredictions. better to rely on true lby updates (OnPlayerStand)

	// update lby timer when they're moving
	// data.m_flNextBodyUpdate = pRecord->m_flAnimationTime + 0.22f;

	// we can mark them as resolved here
	pRecord->m_bIsResolved = true;
	pRecord->m_eConfidence = EConfidence::CONF_VHIGH;
}

void Resolver::OnPlayerJump( LagRecord_t *pRecord, LagRecord_t *pPrevious ) {
	// get data, and reset missed shots on other stages
	auto &data = GetResolverData( pRecord->m_pEntity->EntIndex( ) );
	data.UpdateMissedShots( EResolverStages::RES_AIR );

	// not sure if they're resolved for definite yet
	pRecord->m_bIsResolved = false;

	pRecord->m_eConfidence = EConfidence::CONF_LOW;

	// le awesome we have valid previous record
	// let's see wtf was happening
	if( pPrevious ) {
		// we don't have to check latest record flags
		// here since we're already in OnPlayerJump which only
		// gets triggered when latest flags are in air

		// player left ground (or bhopped, or lby updated etc) 
		if( ( pPrevious->m_fPredFlags & FL_ONGROUND && pPrevious->m_vecPredVelocity.Length2D( ) > 0.1f ) ||
			pRecord->m_flLowerBodyYawTarget != pPrevious->m_flLowerBodyYawTarget ) {
			pRecord->m_iResolverType = 1;
			pRecord->m_szResolver = XorStr( "air body update" );

			// nothing else needs to be done here
			// and we can safeuly assume he's resolved here
			pRecord->m_angEyeAngles.y = pRecord->m_flLowerBodyYawTarget;

			// since lby updated here we can also save stuff here
			// it might help out if they're bhopping or smth
			data.m_flLastMovingBody = pRecord->m_flLowerBodyYawTarget;
			data.m_flLastMovingTime = pRecord->m_flAnimationTime;

			// yeah, we can assume they're resolved here
			pRecord->m_bIsResolved = true;

			if( !pRecord->m_bBrokeTeleportDst )
				pRecord->m_eConfidence = EConfidence::CONF_HIGH;

			return;
		}
	}

	// get missed shots on current resolver stage
	const int nMissed = data.GetMissedShots( EResolverStages::RES_AIR );
	//printf( "%d\n", nMissed );

	// reset this here, we'll be overwriting it anyway
	data.m_bPlausibleBody = false;

	// check if we have valid stuff to work with
	if( data.IsValidFloat( data.m_flLastMovingTime ) &&
		data.IsValidFloat( data.m_flLastMovingBody ) ) {

		// player was recently moving and we logged
		// his lby update, see if his head is still there
		data.m_bPlausibleBody = ( pRecord->m_flAnimationTime - data.m_flLastMovingTime ) < 1.5f;
	}

	// don't start from case 0, the thing 
	switch( nMissed % 4 ) {
		case 0:
			// let's try to aim at lby
			// if it's 'plausible'
			if( data.m_bPlausibleBody ) {
				pRecord->m_iResolverType = 2;
				pRecord->m_szResolver = XorStr( "jump plausible body" );
				pRecord->m_angEyeAngles.y = pRecord->m_flLowerBodyYawTarget;

				if( !pRecord->m_bBrokeTeleportDst )
					pRecord->m_eConfidence = EConfidence::CONF_MED;
			}
			else {
				pRecord->m_iResolverType = 3;
				pRecord->m_szResolver = XorStr( "jump away" );
				pRecord->m_angEyeAngles.y = GetRelativeYaw( pRecord, 180.f );
			}

			break;
		case 1:
			pRecord->m_iResolverType = 4;
			pRecord->m_szResolver = XorStr( "jump neg sideways" );
			pRecord->m_angEyeAngles.y = GetRelativeYaw( pRecord, -90.f );
			break;
		case 2:
			pRecord->m_iResolverType = 5;
			pRecord->m_szResolver = XorStr( "jump pos sideways" );
			pRecord->m_angEyeAngles.y = GetRelativeYaw( pRecord, 90.f );
			break;
		case 3:
			pRecord->m_iResolverType = 6;
			pRecord->m_szResolver = XorStr( "jump direction" );
			pRecord->m_angEyeAngles.y = RAD2DEG( std::atan2( pRecord->m_vecPredVelocity.y, pRecord->m_vecPredVelocity.x ) ) + 180.f;
			break;
	}
}

void Resolver::OnPlayerOverride( LagRecord_t *pRecord, LagRecord_t *pPrevious ) {
	// set our resolver mode
	pRecord->m_iResolverType = 99;
	pRecord->m_szResolver = XorStr( "override" );

	QAngle vecCurView;
	g_pEngine->GetViewAngles( vecCurView );

	float flFinalAngle = 0.f;

	const float flRelativePitch = GetRelativePitch( pRecord, 0.f );
	const float flRelativeYaw = GetRelativeYaw( pRecord, 180.f );

	float flDeltaPitch = Math::AngleNormalize( vecCurView.x - flRelativePitch );
	float flDeltaYaw = Math::AngleNormalize( vecCurView.y - flRelativeYaw );

	// delta yaw is close, we are on the player, override backwards (or forwards)
	if( std::abs( flDeltaYaw ) <= 5.f ) {
		// pitch greater then 0, override forward
		if( flDeltaPitch > 0 ) {
			flFinalAngle = flRelativeYaw + 180.f;
			pRecord->m_szResolver += XorStr( " FORWARD" );
		}

		// else override backwards
		else {
			flFinalAngle = flRelativeYaw;
			pRecord->m_szResolver += XorStr( " BACKWARD" );
		}
	}

	// delta greater then zero, override left
	else if( flDeltaYaw > 0 ) {
		flFinalAngle = flRelativeYaw + 90.f;
		pRecord->m_szResolver += XorStr( " LEFT" );
	}

	// else override right
	else {
		flFinalAngle = flRelativeYaw - 90.f;
		pRecord->m_szResolver += XorStr( " RIGHT" );
	}

	// set our angles
	pRecord->m_angEyeAngles.y = flFinalAngle;
}

void Resolver::OnPlayerResolve( LagRecord_t *pRecord, LagRecord_t *pPrevious ) {
	if( !pRecord )
		return;

	if( pRecord->m_eResolverStage == EResolverStages::RES_NONE )
		return;

	// before we resolve, let's save some useful data
	// that we could use to our advantage
	auto &data = GetResolverData( pRecord->m_pEntity->EntIndex( ) );
	data.m_flFreestandYaw = GetFreestandYaw( pRecord );

	// reset this, so we can overwrite 
	// it when edge doesn't fail
	data.m_flEdgeYaw = -1.f;

	// run our edge anti-aim on this player 
	// to get an ideal edge angle for them
	if( QAngle angEdge; g_AntiAim.DoEdgeAntiAim( pRecord->m_pEntity, angEdge ) ) {
		data.m_flEdgeYaw = angEdge.y;
	}

	GetApproximateBodyDelta( pRecord, pPrevious );

	if( pRecord->m_eResolverStage == EResolverStages::RES_AIR ) {
		// reset lby pred stuff and deduction stuff too
		data.ResetStageSpecific( EResolverStages::RES_STAND );

		return OnPlayerJump( pRecord, pPrevious );
	}

	if( pRecord->m_eResolverStage == EResolverStages::RES_MOVE ) {
		// reset lby pred stuff and deduction stuff too
		data.ResetStageSpecific( EResolverStages::RES_STAND );

		return OnPlayerMove( pRecord, pPrevious );
	}

	if( pRecord->m_eResolverStage == EResolverStages::RES_OVERRIDE ) {
		// reset lby pred stuff and deduction stuff too
		data.ResetStageSpecific( EResolverStages::RES_STAND );

		return OnPlayerOverride( pRecord, pPrevious );
	}

	// see if the player is breaking lby

	// let's run this here, after OnPlayerMove 
	// so we have the latest info about moving player
	GetApproximateBodyState( pRecord, pPrevious );

	return g_Vars.rage.resolver_trial ? OnPlayerStandTrial( pRecord, pPrevious ) : OnPlayerStand( pRecord, pPrevious );
}

void Resolver::ResolvePlayers( LagRecord_t *pRecord, LagRecord_t *pPrevious ) {
	const auto pLocal = C_CSPlayer::GetLocalPlayer( );
	if( !pLocal )
		return;

	if( !pRecord )
		return;

	if( !g_Vars.rage.resolver )
		return;

	// don't resolve teammates 
	if( pRecord->m_pEntity->IsTeammate( pLocal ) ) {
		pRecord->m_bIsResolved = true;
		return;
	}

	// don't resolve bots
	player_info_t info;
	if( g_pEngine->GetPlayerInfo( pRecord->m_pEntity->EntIndex( ), &info ) && info.fakeplayer ) {
		pRecord->m_bIsResolved = true;
		pRecord->m_szResolver = XorStr( "bot" );
		return;
	}

	// easy resolver data accessor
	auto &data = GetResolverData( pRecord->m_pEntity->EntIndex( ) );

	auto ulSteamID = pRecord->m_pEntity->GetSteamID( );

	// steam id somehow changed for this player ??
	// reset shit that we don't usually reset...
	if( data.m_ulSteamID != NULL && data.m_ulSteamID != ulSteamID ) {
		data.Reset( );

		// important
		data.m_nMissedBody = -1;
		data.m_bMissedFreestand = false;
	}

	// store player's steam id
	data.m_ulSteamID = ulSteamID;

	// update our resolver stage first
	pRecord->m_eResolverStage = UpdateResolverStage( pRecord );

	// resolve le player
	OnPlayerResolve( pRecord, pPrevious );

	// correct pitch of shooting players
	CorrectShotRecord( pRecord );

	// write angles to our player
	pRecord->m_pEntity->m_angEyeAngles( ).y = pRecord->m_angEyeAngles.y = Math::AngleNormalize( pRecord->m_angEyeAngles.y );
	pRecord->m_pEntity->m_angEyeAngles( ).x = pRecord->m_angEyeAngles.x = Math::AngleNormalize( pRecord->m_angEyeAngles.x );
}