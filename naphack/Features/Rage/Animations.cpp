#include "Animations.hpp"
#include "Resolver.hpp"
#include "ServerAnimations.hpp"
#include "../Visuals/Models.hpp"

/* ^^^ finally no need for 1000 disgusting includes */

void Animations::UpdateLerpTime( ) {
	float flLerpRatio = g_Vars.cl_interp_ratio->GetFloat( );
	if( flLerpRatio <= 0.0f )
		flLerpRatio = 1.0f;

	if( const float flMinInterp = g_Vars.sv_client_min_interp_ratio->GetFloat( );  flMinInterp != -1 )
		flLerpRatio = Math::Clamp( flLerpRatio, flMinInterp, g_Vars.sv_client_max_interp_ratio->GetFloat( ) );

	float flUpdateRate = Math::Clamp( g_Vars.cl_updaterate->GetFloat( ), g_Vars.sv_minupdaterate->GetFloat( ), g_Vars.sv_maxupdaterate->GetFloat( ) );
	if( flUpdateRate == 0.f )
		return;

	m_fLerpTime = std::fmaxf( g_Vars.cl_interp->GetFloat( ), flLerpRatio / flUpdateRate );
}

void Animations::UpdateLatency( ) {
	auto pNetChannel = Encrypted_t<INetChannel>( g_pEngine->GetNetChannelInfo( ) );
	if( !pNetChannel.IsValid( ) )
		return;

	auto pLocal = C_CSPlayer::GetLocalPlayer( );
	if( !pLocal )
		return;

	m_flOutgoingLatency = pNetChannel->GetLatency( FLOW_OUTGOING );
	m_flIncomingLatency = pNetChannel->GetLatency( FLOW_INCOMING );

	int nPredictedArrival = g_pClientState->m_ClockDriftManager( ).m_nServerTick + 1 + TIME_TO_TICKS( m_flOutgoingLatency );

	// account for chokes ticked when we're fakewalking
	if( g_Vars.globals.m_bFakeWalking ) {
		// this is how we do it in our fakewalk, so ye
		const int nTicksTillFlick = TIME_TO_TICKS( g_ServerAnimations.m_uServerAnimations.m_flLowerBodyRealignTimer ) - ( pLocal->m_nTickBase( ) + 2 );
		int nFakewalkChoke = g_Vars.sv_maxusrcmdprocessticks->GetInt( ) - 2;
		if( nFakewalkChoke > nTicksTillFlick )
			nFakewalkChoke = nTicksTillFlick;

		nPredictedArrival += std::max( 1, nFakewalkChoke - g_pClientState->m_nChokedCommands( ) );
	}

	m_nArrivalTick = nPredictedArrival;
}

void LagRecord_t::SetupRecord( C_CSPlayer *pEntity, bool bBackup ) {
	if( !pEntity )
		return;

	m_flCreationTime = g_pGlobalVars->realtime;

	m_bExtrapolated = false;

	m_nPredictedTicks = 0;

	m_pEntity = pEntity;

	m_bSimTick = false;

	m_nEntIndex = pEntity->EntIndex( );

	m_pWeapon = reinterpret_cast< C_WeaponCSBaseGun * >( pEntity->m_hActiveWeapon( ).Get( ) );

	m_nServerTick = g_pClientState->m_ClockDriftManager( ).m_nServerTick;

	m_flPredSimulationTime = m_flSimulationTime = pEntity->m_flSimulationTime( );
	m_flLowerBodyYawTarget = pEntity->m_flLowerBodyYawTarget( );
	m_flDuckAmount = pEntity->m_flDuckAmount( );

	m_bGunGameImmunity = pEntity->m_bGunGameImmunity( );

	m_fPredFlags = m_fFlags = pEntity->m_fFlags( );
	m_flChokedTime = m_flSimulationTime - pEntity->m_flOldSimulationTime( );
	m_nChokedTicks = std::clamp( TIME_TO_TICKS( m_flChokedTime ), 0, 16 );

	m_vecMins = pEntity->OBBMins( );
	m_vecMaxs = pEntity->OBBMaxs( );

	m_vecPredOrigin = m_vecOrigin = pEntity->m_vecOrigin( );
	m_vecAbsOrigin = pEntity->GetAbsOrigin( );

	m_vecPredVelocity = m_vecVelocity = pEntity->m_vecVelocity( );
	m_vecAbsVelocity = pEntity->GetAbsVelocity( );

	m_angEyeAngles = pEntity->m_angEyeAngles( );
	m_sAnims[ ESides::SIDE_SERVER ].m_angAbsAngles = pEntity->GetAbsAngles( );

	m_vecEyePosition = pEntity->GetEyePosition( );
	m_vecLowerChestPosition = pEntity->GetHitboxPosition( HITBOX_LOWER_CHEST );
	m_vecChestPosition = pEntity->GetHitboxPosition( HITBOX_CHEST );
	m_vecPelvisPosition = pEntity->GetHitboxPosition( HITBOX_PELVIS );
	m_vecStomachPosition = pEntity->GetHitboxPosition( HITBOX_STOMACH );

	if( pEntity->m_hActiveWeapon( ).IsValid( ); auto pWeapon = ( C_WeaponCSBaseGun * )pEntity->m_hActiveWeapon( ).Get( ) ) {
		m_fLastShotTime = pWeapon ? pWeapon->m_fLastShotTime( ) : 0.f;
	}

	std::memcpy( m_pBackupMatrix, pEntity->m_CachedBoneData( ).Base( ), pEntity->m_CachedBoneData( ).Count( ) * sizeof( matrix3x4_t ) );

	m_bIsBackup = bBackup;
}

void LagRecord_t::ApplyRecord( C_CSPlayer *pEntity ) {
	if( !pEntity || pEntity == nullptr || pEntity->EntIndex( ) == 0 || pEntity->EntIndex( ) > 64 || g_Vars.globals.m_bForceFullUpdate || g_pClientState->m_nDeltaTick( ) == -1 )
		return;

	const auto pSaneEntity = reinterpret_cast< C_CSPlayer * >( g_pEntityList->GetClientEntity( pEntity->EntIndex( ) ) );

	if( !pSaneEntity || pSaneEntity->m_entIndex == 0 || pSaneEntity->m_entIndex > 64 || pSaneEntity == nullptr || !m_flSimulationTime )
		return;

	// pSaneEntity->m_flSimulationTime( ) = m_flSimulationTime;
	pSaneEntity->m_flLowerBodyYawTarget( ) = m_flLowerBodyYawTarget;
	pSaneEntity->m_flDuckAmount( ) = m_flDuckAmount;

	pSaneEntity->m_fFlags( ) = m_fPredFlags;

	if( auto pCollision = pSaneEntity->m_Collision( ); pCollision ) {
		pCollision->SetCollisionBounds( m_vecMins, m_vecMaxs );
	}

	pSaneEntity->m_vecOrigin( ) = m_vecPredOrigin;
	pSaneEntity->SetAbsOrigin( pSaneEntity->m_vecOrigin( ) );

	pSaneEntity->m_vecVelocity( ) = m_vecPredVelocity;
	pSaneEntity->SetAbsVelocity( pSaneEntity->m_vecVelocity( ) );

	pSaneEntity->m_angEyeAngles( ) = m_angEyeAngles;
	pSaneEntity->SetAbsAngles( m_sAnims[ ESides::SIDE_SERVER ].m_angAbsAngles );

	std::memcpy( pSaneEntity->m_CachedBoneData( ).Base( ), m_bIsBackup ? m_pBackupMatrix : m_sAnims[ ESides::SIDE_SERVER ].m_pMatrix, pSaneEntity->m_CachedBoneData( ).Count( ) * sizeof( matrix3x4_t ) );
}

bool LagRecord_t::IsRecordValid( bool bSkipDeadTime ) {
	C_CSPlayer *pLocal = C_CSPlayer::GetLocalPlayer( );
	if( !pLocal )
		return false;

	if( !g_pClientState.Xor( ) )
		return false;

	if( !this )
		return false;

	if( m_bInvalid )
		return false;

	// https://git.new-page.xyz/newpage/csgo/cstrike15_src/-/blob/master/game/server/player_lagcompensation.cpp#L268-297

	// correct is the amount of time we have to correct game time
	float correct = 0.f;

	// add network latency
	correct += g_Animations.m_flOutgoingLatency + g_Animations.m_flIncomingLatency;

	// NOTE:  do these computations in float time, not ticks, to avoid big roundoff error accumulations in the math
	// add view interpolation latency see C_BaseEntity::GetInterpolationAmount()
	correct += g_Animations.m_fLerpTime;

	// check bounds [0,sv_maxunlag]
	correct = std::clamp( correct, 0.0f, g_Vars.sv_maxunlag->GetFloat( ) );

	// correct tick send by player 
	float flTargetTime = m_flSimulationTime;

	// calculate difference between tick sent by player and our latency based tick
	float deltaTime = correct - ( TICKS_TO_TIME( pLocal->m_nTickBase( ) ) - flTargetTime );

	if( fabs( deltaTime ) >= 0.2f ) {
		// difference between cmd time and latency is too big > 200ms, use time correction based on latency
		// DevMsg("StartLagCompensation: delta too big (%.3f)\n", deltaTime );
		return false;
	}

	// https://git.new-page.xyz/newpage/csgo/cstrike15_src/-/blob/master/game/server/player_lagcompensation.cpp#L700
	// pretty retarded, but since this is an int we lose float precision, so we have to account for this...
	if( !bSkipDeadTime ) {
		int nExtraChoke = 0;
		if( g_Vars.globals.m_bFakeWalking ) {
			const int nTicksTillFlick = TIME_TO_TICKS( g_ServerAnimations.m_uServerAnimations.m_flLowerBodyRealignTimer ) - ( pLocal->m_nTickBase( ) + 2 );
			int nFakewalkChoke = g_Vars.sv_maxusrcmdprocessticks->GetInt( ) - 2;
			if( nFakewalkChoke > nTicksTillFlick )
				nFakewalkChoke = nTicksTillFlick;

			nExtraChoke = std::clamp( nFakewalkChoke - g_pClientState->m_nChokedCommands( ), 0, g_Vars.sv_maxusrcmdprocessticks->GetInt( ) );
		}

		// calculate what "gpGlobals->curtime" will be during lag compensation
		const float flLagcompensationCurtime = TICKS_TO_TIME( g_pGlobalVars->tickcount + TIME_TO_TICKS( g_Animations.m_flOutgoingLatency + g_Animations.m_flIncomingLatency ) + nExtraChoke );

		int nDeadTime = flLagcompensationCurtime - g_Vars.sv_maxunlag->GetFloat( );
		if( TIME_TO_TICKS( m_flSimulationTime + g_Animations.m_fLerpTime ) < nDeadTime )
			return false;
	}

	return true;
}

void Animations::AnimationEntry_t::UpdatePlayer( LagRecord_t *pRecord ) {
	// probably call from UpdatePlayerSimple
	if( !m_pEntity ) {
		m_pEntity = pRecord->m_pEntity;
	}

	if( !m_pEntity ) {
		return;
	}

	CCSGOPlayerAnimState *const pState = m_pEntity->m_PlayerAnimState( );
	if( !pState ) {
		return;
	}

	// backup data before changes
	const float flBackupCurtime = g_pGlobalVars->curtime;
	const float flBackupFrametime = g_pGlobalVars->frametime;

	// allow re-animating this frame
	if( pState->m_nLastUpdateFrame >= g_pGlobalVars->framecount ) {
		pState->m_nLastUpdateFrame = g_pGlobalVars->framecount - 1;
	}

	// allow re-animating this frame
	if( pState->m_flLastUpdateTime >= g_pGlobalVars->curtime ) {
		pState->m_flLastUpdateTime = g_pGlobalVars->curtime - g_pGlobalVars->interval_per_tick;
	}

	// mimic global vars during server lag compensation
	g_pGlobalVars->curtime = pRecord->m_flAnimationTime;
	g_pGlobalVars->frametime = g_pGlobalVars->interval_per_tick;

	// prevents some sus crash where layers go invalid 
	// cos the owner of them is nullptr o_O idk why
	if( m_pEntity->m_AnimOverlay( ).Count( ) ) {
		for( int i = 0; i < pRecord->m_pEntity->m_AnimOverlay( ).Count( ); ++i ) {
			m_pEntity->m_AnimOverlay( ).Base( )[ i ].m_pOwner = m_pEntity;
			m_pEntity->m_AnimOverlay( ).Base( )[ i ].m_pStudioHdr = m_pEntity->m_pStudioHdr( );
		}
	}

	// skip call to absvelocity and force the game to accept our absvelocity
	m_pEntity->m_iEFlags( ) &= ~EFL_DIRTY_ABSVELOCITY;
	//m_pEntity->m_iEFlags( ) &= ~EFL_DIRTY_ABSTRANSFORM;

	// update animations
	m_pEntity->m_bClientSideAnimation( ) = true;
	m_pEntity->UpdateClientSideAnimation( );
	m_pEntity->m_bClientSideAnimation( ) = false;

	// mark an animation update for the game and invalidate physics
	//m_pEntity->InvalidatePhysicsRecursive( ANIMATION_CHANGED /*| ANGLES_CHANGED | SEQUENCE_CHANGED*/ );

	// restore globals
	g_pGlobalVars->curtime = flBackupCurtime;
	g_pGlobalVars->frametime = flBackupFrametime;
}

void Animations::AnimationEntry_t::SimulateSideAnimation( LagRecord_t *pRecord, LagRecord_t *pPrevious, ESides eSide, float flTargetAbs ) {
	// setup variables we need to backup, restore later
	CCSGOPlayerAnimState pBackupState;
	pBackupState = *m_pEntity->m_PlayerAnimState( );

	matrix3x4_t pBackupMatrix[ MAXSTUDIOBONES ];
	std::memcpy( pBackupMatrix, m_pEntity->m_CachedBoneData( ).Base( ), m_pEntity->m_CachedBoneData( ).Count( ) * sizeof( matrix3x4_t ) );

	std::array<C_AnimationLayer, 13> pBackupAnimOverlays;
	std::memcpy( pBackupAnimOverlays.data( ), m_pEntity->m_AnimOverlay( ).Base( ), sizeof( C_AnimationLayer ) * 13 );

	std::array<float, 20> pBackupPosesParameters;
	std::memcpy( pBackupPosesParameters.data( ), m_pEntity->m_flPoseParameter( ), sizeof( float ) * 20 );

	// if we've previously filled this entry use the prev animstate 
	if( pRecord->m_sAnims[ eSide ].m_bUpdated ) {
		*m_pEntity->m_PlayerAnimState( ) = pRecord->m_sAnims[ eSide ].m_pAnimState;
	}

	if( !pRecord->m_sAnims[ eSide ].m_bSimulated ) {
		m_pEntity->m_PlayerAnimState( )->m_flFootYaw = pRecord->m_flLowerBodyYawTarget + flTargetAbs;
		pRecord->m_sAnims[ eSide ].m_bSimulated = true;
	}
	else {
		if( pPrevious )
			m_pEntity->m_PlayerAnimState( )->m_flFootYaw = pPrevious->m_sAnims[ eSide ].m_flFootYaw;
	}

	UpdatePlayer( pRecord );

	// printf( "[%i]: post anim update: %.4f, %.4f\n", int( eSide ), m_pEntity->m_PlayerAnimState( )->m_flFootYaw, Math::AngleDiff( pRecord->m_sAnims[ eSide ].m_flFootYaw, m_pEntity->m_PlayerAnimState( )->m_flEyeYaw ) );

	// store layers, poses, animstate, etc
	pRecord->m_sAnims[ eSide ].m_bUpdated = true;
	pRecord->m_sAnims[ eSide ].m_pAnimState = *m_pEntity->m_PlayerAnimState( );
	pRecord->m_sAnims[ eSide ].m_flFootYaw = m_pEntity->m_PlayerAnimState( )->m_flFootYaw;
	pRecord->m_sAnims[ eSide ].m_angAbsAngles = m_pEntity->GetAbsAngles( );

	std::memcpy( pRecord->m_sAnims[ eSide ].m_pPoseParameters.data( ), m_pEntity->m_flPoseParameter( ), sizeof( float ) * 20 );
	std::memcpy( pRecord->m_sAnims[ eSide ].m_pServerAnimOverlays.data( ), m_pEntity->m_AnimOverlay( ).Base( ), sizeof( C_AnimationLayer ) * 13 );

	// generate a matrix with our le simulated data
	pRecord->m_sAnims[ eSide ].m_pBoneBuilder.store( pRecord, BONE_USED_BY_SERVER, eSide );
	pRecord->m_sAnims[ eSide ].m_pBoneBuilder.setup( );

	// restore to previously stored data
	*m_pEntity->m_PlayerAnimState( ) = pBackupState;
	std::memcpy( m_pEntity->m_AnimOverlay( ).Base( ), pBackupAnimOverlays.data( ), sizeof( C_AnimationLayer ) * 13 );
	std::memcpy( m_pEntity->m_flPoseParameter( ), pBackupPosesParameters.data( ), sizeof( float ) * 20 );
	std::memcpy( m_pEntity->m_CachedBoneData( ).Base( ), pBackupMatrix, m_pEntity->m_CachedBoneData( ).Count( ) * sizeof( matrix3x4_t ) );
}

void Animations::AnimationEntry_t::UpdateAnimations( LagRecord_t *pRecord ) {
	// backup stuff we don't really want to mess with
	const int m_fFlags = m_pEntity->m_fFlags( );
	const int m_iEFlags = m_pEntity->m_iEFlags( );

	const float m_flDuckAmount = m_pEntity->m_flDuckAmount( );
	const float m_flLowerBodyYawTarget = m_pEntity->m_flLowerBodyYawTarget( );

	const Vector m_vecOrigin = m_pEntity->m_vecOrigin( );
	const Vector m_vecAbsOrigin = m_pEntity->GetAbsOrigin( );
	const Vector m_vecVelocity = m_pEntity->m_vecVelocity( );
	const Vector m_vecAbsVelocity = m_pEntity->GetAbsVelocity( );

	// backup our bone matrix
	matrix3x4_t pBackupMatrix[ MAXSTUDIOBONES ];
	const int nBoneCount = m_pEntity->m_CachedBoneData( ).Count( );
	std::memcpy( pBackupMatrix, m_pEntity->m_CachedBoneData( ).Base( ), nBoneCount * sizeof( matrix3x4_t ) );

	LagRecord_t *pPrevious = nullptr,
		*pPenult = nullptr;

	if( m_deqRecords.size( ) >= 2 ) {
		pPrevious = &m_deqRecords.at( 1 );

		if( m_deqRecords.size( ) >= 3 )
			pPenult = &m_deqRecords.at( 2 );
	}

	// update animation time
	pRecord->m_flAnimationTime = ( pPrevious ? pPrevious->m_flSimulationTime : m_pEntity->m_flOldSimulationTime( ) ) + g_pGlobalVars->interval_per_tick;

	if( !pPrevious ) {
		pRecord->m_bBrokeTeleportDst = true;//m_pEntity->m_vecOrigin( ).DistanceSquared( m_pEntity->m_vecOldOrigin( ) ) > 4096.f;
	}

	pRecord->m_flDurationInAir = m_pEntity->m_PlayerAnimState( ) ? m_pEntity->m_PlayerAnimState( )->m_flDurationInAir : 0.f;
	pRecord->m_flChokedTime = pRecord->m_flSimulationTime - ( pPrevious ? pPrevious->m_flSimulationTime : m_pEntity->m_flOldSimulationTime( ) );
	pRecord->m_nChokedTicks = std::clamp( TIME_TO_TICKS( pRecord->m_flChokedTime ), 0, 16 );

	if( pPrevious ) {
		pRecord->m_flAnimationTime = pPrevious->m_flSimulationTime + g_pGlobalVars->interval_per_tick;
		pRecord->m_flChokedTime = pRecord->m_flSimulationTime - pPrevious->m_flSimulationTime;
		pRecord->m_nChokedTicks = std::clamp( TIME_TO_TICKS( pRecord->m_flChokedTime ), 0, 16 );

		// save origin delta
		pRecord->m_vecOriginDelta = ( pRecord->m_vecOrigin - pPrevious->m_vecOrigin );

		// generate animation velocity based on origin change
		pRecord->m_vecVelocity = pRecord->m_vecOriginDelta / pRecord->m_flChokedTime;

		if( /*pRecord->m_flDurationInAir == 0.f &&*/
			pRecord->m_fFlags & FL_ONGROUND &&
			pPrevious->m_pWeapon == pRecord->m_pWeapon &&
			pRecord->m_sAnims[ ESides::SIDE_SERVER ].m_pServerAnimOverlays[ ANIMATION_LAYER_ALIVELOOP ].m_flPlaybackRate != pPrevious->m_sAnims[ ESides::SIDE_SERVER ].m_pServerAnimOverlays[ ANIMATION_LAYER_ALIVELOOP ].m_flPlaybackRate ) {
			const float flWeight = pRecord->m_sAnims[ ESides::SIDE_SERVER ].m_pServerAnimOverlays[ ANIMATION_LAYER_ALIVELOOP ].m_flWeight;

			// compute a general animation modifier
			// CCSGOPlayerAnimationState::SetupMovement
			const auto flSpeed =
				( 0.55f - ( ( flWeight - 1.f ) * 0.35f ) )
				* ( pRecord->m_pWeapon ? std::max( 0.1f, pRecord->m_pWeapon->GetMaxSpeed( ) ) : 260.f );

			const auto flAverageSpeed = pRecord->m_vecVelocity.Length2D( );

			// modify accordingly
			if( ( flWeight >= 1.f && flAverageSpeed > flSpeed )
				|| ( flWeight < 1.f && ( flSpeed >= flAverageSpeed || flWeight > 0.f ) ) ) {
				if( flAverageSpeed ) {
					pRecord->m_vecVelocity.x /= flAverageSpeed;
					pRecord->m_vecVelocity.y /= flAverageSpeed;
				}

				pRecord->m_vecVelocity.x *= flSpeed;
				pRecord->m_vecVelocity.y *= flSpeed;
			}
		}

		// overwrite this with stored record data
		pRecord->m_bBrokeTeleportDst = false;

		// detect players breaking the teleport distance
		// https://github.com/perilouswithadollarsign/cstrike15_src/blob/master/game/server/player_lagcompensation.cpp#L384-L388
		if( pRecord->m_vecOriginDelta.Length2DSquared( ) > 4096.f )
			pRecord->m_bBrokeTeleportDst = true;

		if( &pRecord && &m_deqRecords[ 1 ] ) {
			if( ( ( pRecord->m_vecOrigin - m_deqRecords[ 1 ].m_vecOrigin ).Length2DSquared( ) > 4096.f ) ) {
				pRecord->m_bBrokeTeleportDst = true;
			}
		}

		if( &m_deqRecords[ 1 ] && &m_deqRecords[ 2 ] ) {
			if( ( m_deqRecords[ 1 ].m_vecOrigin - m_deqRecords[ 2 ].m_vecOrigin ).Length2DSquared( ) > 4096.f ) {
				pRecord->m_bBrokeTeleportDst = true;
			}
		}

		pRecord->m_nServerDeltaTicks = pRecord->m_nServerTick - TIME_TO_TICKS( pRecord->m_flSimulationTime );

		// TODO: seems pretty accurate the most time, though still look into this
		if( pRecord->m_nChokedTicks < 17 ) {
			pRecord->m_bShiftingTickbase = ( ( pRecord->m_nServerDeltaTicks >= 11 ) && ( ( pRecord->m_nChokedTicks < 3 ) ) );
			if( !pRecord->m_bShiftingTickbase ) {
				if( pPrevious->m_bShiftingTickbase && pRecord->m_nChokedTicks <= pPrevious->m_nChokedTicks && pRecord->m_nServerDeltaTicks >= pRecord->m_nChokedTicks ) {
					pRecord->m_bShiftingTickbase = true;
				}
			}
		}

		// detect 'fake-walking' players
		// when the playback of the move layer is zero or near zero this means the player
		// isn't moving, the only way this will trigger is when they're actually standing still (or fakewalking)
		if( !pRecord->m_vecOriginDelta.IsZero( ) ) {
			// make sure this doesn't trigger on in air players (invoking our stand resolver)
			// player can't be fakewalking if above their max weapon speed, or not on ground
			if( m_pEntity->m_fFlags( ) & FL_ONGROUND && pRecord->m_vecVelocity.Length( ) <= m_pEntity->GetMaxSpeed( ) * 0.3000001 ) {
				if( pRecord->m_sAnims[ ESides::SIDE_SERVER ].m_pServerAnimOverlays[ ANIMATION_LAYER_MOVEMENT_MOVE ].m_flPlaybackRate == 0.f ||
					( pRecord->m_sAnims[ ESides::SIDE_SERVER ].m_pServerAnimOverlays[ ANIMATION_LAYER_MOVEMENT_MOVE ].m_flWeight != 1.f &&
					  pRecord->m_sAnims[ ESides::SIDE_SERVER ].m_pServerAnimOverlays[ ANIMATION_LAYER_MOVEMENT_MOVE ].m_flPlaybackRate == pPrevious->m_sAnims[ ESides::SIDE_SERVER ].m_pServerAnimOverlays[ ANIMATION_LAYER_MOVEMENT_MOVE ].m_flPlaybackRate &&
					  pRecord->m_sAnims[ ESides::SIDE_SERVER ].m_pServerAnimOverlays[ ANIMATION_LAYER_MOVEMENT_MOVE ].m_flWeight == pPrevious->m_sAnims[ ESides::SIDE_SERVER ].m_pServerAnimOverlays[ ANIMATION_LAYER_MOVEMENT_MOVE ].m_flWeight ) ) {
					// due to the playback rate of the movement layer, their
					// server velocity will also be zero
					pRecord->m_vecVelocity.Init( );
				}
			}
		}
	}

	// sanitise the velocity
	if( pRecord->m_vecVelocity.IsInvalid( ) )
		pRecord->m_vecVelocity.Init( );

	// copy over just to be sure, it will be
	// overwritten by prediction anyway
	pRecord->m_vecPredVelocity = pRecord->m_vecVelocity;

	// apply fixed record data to the player
	// and resolve/update animations with it
	pRecord->ApplyRecord( m_pEntity );

	if( pPrevious && pRecord->m_nChokedTicks > 0 ) {
		// fix JUMP_FALL pose
		m_pEntity->m_fFlags( ) = pPrevious->m_fFlags;
		m_pEntity->m_fFlags( ) &= ~FL_ONGROUND;

		if( pRecord->m_fFlags & FL_ONGROUND && pPrevious->m_fFlags & FL_ONGROUND ) {
			m_pEntity->m_fFlags( ) |= FL_ONGROUND;
		}

		if( pRecord->m_sAnims[ ESides::SIDE_SERVER ].m_pServerAnimOverlays[ ANIMATION_LAYER_MOVEMENT_JUMP_OR_FALL ].m_flWeight != 1.f &&
			pPrevious->m_sAnims[ ESides::SIDE_SERVER ].m_pServerAnimOverlays[ ANIMATION_LAYER_MOVEMENT_JUMP_OR_FALL ].m_flWeight == 1.f &&
			pRecord->m_sAnims[ ESides::SIDE_SERVER ].m_pServerAnimOverlays[ ANIMATION_LAYER_MOVEMENT_LAND_OR_CLIMB ].m_flWeight != 0.f ) {
			m_pEntity->m_fFlags( ) |= FL_ONGROUND;
		}

		if( pRecord->m_fFlags & FL_ONGROUND && !( pPrevious->m_fFlags & FL_ONGROUND ) )
			m_pEntity->m_fFlags( ) &= ~FL_ONGROUND;

		const auto flLerp = 1.f - ( pRecord->m_flSimulationTime - pRecord->m_flAnimationTime ) / ( pRecord->m_flSimulationTime - pPrevious->m_flSimulationTime );

		// In numerical analysis, a cubic Hermite spline or cubic Hermite interpolator is a spline where each piece is a third-degree polynomial specified in Hermite form, that is, by its values and first derivatives at the end points of the corresponding domain interval.[1]
		if( pPenult )
			pRecord->m_pEntity->m_flDuckAmount( ) = Math::Hermite_Spline( pPenult->m_flDuckAmount, pPrevious->m_flDuckAmount, pRecord->m_flDuckAmount, flLerp );
		// regualrt linear interpolation
		else
			pRecord->m_pEntity->m_flDuckAmount( ) = Math::Interpolate( pPrevious->m_flDuckAmount, pRecord->m_flDuckAmount, flLerp );

		auto pLandLayer = &pRecord->m_sAnims[ ESides::SIDE_SERVER ].m_pServerAnimOverlays[ ANIMATION_LAYER_MOVEMENT_LAND_OR_CLIMB ];

		auto pLandSequence = m_pEntity->GetSequenceActivity( pRecord->m_sAnims[ ESides::SIDE_SERVER ].m_pServerAnimOverlays[ ANIMATION_LAYER_MOVEMENT_LAND_OR_CLIMB ].m_nSequence );
		if( pLandSequence == ACT_CSGO_LAND_LIGHT && m_pEntity->m_fFlags( ) & FL_ONGROUND ) {
			pRecord->m_bPotentialDesync =
				pRecord->m_sAnims[ ESides::SIDE_SERVER ].m_pServerAnimOverlays[ ANIMATION_LAYER_MOVEMENT_LAND_OR_CLIMB ].m_flCycle > 0.1f &&
				// 0.92 is finished
				pRecord->m_sAnims[ ESides::SIDE_SERVER ].m_pServerAnimOverlays[ ANIMATION_LAYER_MOVEMENT_LAND_OR_CLIMB ].m_flCycle < 0.92f &&
				// layer continues to gradually update (first 0.92 tick will be broken, next will be fine)
				pRecord->m_sAnims[ ESides::SIDE_SERVER ].m_pServerAnimOverlays[ ANIMATION_LAYER_MOVEMENT_LAND_OR_CLIMB ].m_flCycle >
				pPrevious->m_sAnims[ ESides::SIDE_SERVER ].m_pServerAnimOverlays[ ANIMATION_LAYER_MOVEMENT_LAND_OR_CLIMB ].m_flCycle;

			// TODO: finish this, not perfect yet

			if( pRecord->m_bPotentialDesync ) {
				//flDistanceFell = abs( m_flLeftGroundHeight - m_vecPositionCurrent.z );
				//float flDistanceFallNormalizedBiasRange = Bias( RemapValClamped( flDistanceFell, 12.0f, 72.0f, 0.0f, 1.0f ), 0.4f );

				//Msg( "Fell %f units, ratio is %f. ", flDistanceFell, flDistanceFallNormalizedBiasRange );
				//Msg( "Fell for %f secs, multiplier is %f\n", m_flDurationInAir, m_flLandAnimMultiplier );

				//m_flLandAnimMultiplier = clamp( Bias( m_flDurationInAir, 0.3f ), 0.1f, 1.0f );
				//m_flDuckAdditional = MAX( m_flLandAnimMultiplier, flDistanceFallNormalizedBiasRange );

				pRecord->m_pEntity->m_flDuckAmount( ) -= m_pEntity->m_PlayerAnimState( )->m_flDuckAdditional * 0.45f;
			}
		}

	}

	// save original record flags.
	const auto fBackupFlags = pRecord->m_fFlags;

	// apply the corrected flags.
	// the resolver uses the record's flags
	pRecord->m_fFlags = pRecord->m_fPredFlags = m_pEntity->m_fFlags( );

	// attempt to resolve this player
	// this directly writes to m_angEyeAngles as
	// well as record eye angles, therefore we don't
	// have to worry about them getting overwritten
	g_Resolver.ResolvePlayers( pRecord, pPrevious );

	// restore them.
	pRecord->m_fFlags = fBackupFlags;

	// player is moving
	if( pRecord->m_vecVelocity.Length2D( ) > 0.1f ) {
		if( pPrevious ) {
			// we've simulated before
			if( pPrevious->m_sAnims[ ESides::SIDE_OPPOSITEL ].m_bSimulated )
				pRecord->m_sAnims[ ESides::SIDE_OPPOSITEL ].m_bSimulated = true;

			// we've simulated before
			if( pPrevious->m_sAnims[ ESides::SIDE_OPPOSITER ].m_bSimulated )
				pRecord->m_sAnims[ ESides::SIDE_OPPOSITER ].m_bSimulated = true;

			if( pPrevious->m_sAnims[ ESides::SIDE_MIDDLE ].m_bSimulated )
				pRecord->m_sAnims[ ESides::SIDE_MIDDLE ].m_bSimulated = true;

			// SimulateSideAnimation( pRecord, pPrevious, ESides::SIDE_OPPOSITEL, 60.f );
			// SimulateSideAnimation( pRecord, pPrevious, ESides::SIDE_OPPOSITER, -60.f );
			// SimulateSideAnimation( pRecord, pPrevious, ESides::SIDE_MIDDLE, 0.f );

			const float flRightPlaybackDeltaToServer = fabsf( pRecord->m_sAnims[ ESides::SIDE_SERVER ].m_pServerAnimOverlays[ ANIMATION_LAYER_MOVEMENT_MOVE ].m_flPlaybackRate -
															  pRecord->m_sAnims[ ESides::SIDE_OPPOSITER ].m_pServerAnimOverlays[ ANIMATION_LAYER_MOVEMENT_MOVE ].m_flPlaybackRate );

			const float flLeftPlaybackDeltaToServer = fabsf( pRecord->m_sAnims[ ESides::SIDE_SERVER ].m_pServerAnimOverlays[ ANIMATION_LAYER_MOVEMENT_MOVE ].m_flPlaybackRate -
															 pRecord->m_sAnims[ ESides::SIDE_OPPOSITEL ].m_pServerAnimOverlays[ ANIMATION_LAYER_MOVEMENT_MOVE ].m_flPlaybackRate );

			const float flMiddlePlaybackDeltaToServer = fabsf( pRecord->m_sAnims[ ESides::SIDE_SERVER ].m_pServerAnimOverlays[ ANIMATION_LAYER_MOVEMENT_MOVE ].m_flPlaybackRate -
															   pRecord->m_sAnims[ ESides::SIDE_MIDDLE ].m_pServerAnimOverlays[ ANIMATION_LAYER_MOVEMENT_MOVE ].m_flPlaybackRate );

			const float flSmallestDelta = std::min( { flRightPlaybackDeltaToServer, flLeftPlaybackDeltaToServer, flMiddlePlaybackDeltaToServer } );

			if( flSmallestDelta == flRightPlaybackDeltaToServer && fabsf( Math::AngleDiff( pRecord->m_sAnims[ ESides::SIDE_OPPOSITER ].m_flFootYaw, pRecord->m_sAnims[ ESides::SIDE_SERVER ].m_flFootYaw ) ) > 5.f ) {
				pRecord->m_iChosenMatrix = 1;
			}
			else if( flSmallestDelta == flLeftPlaybackDeltaToServer && fabsf( Math::AngleDiff( pRecord->m_sAnims[ ESides::SIDE_OPPOSITEL ].m_flFootYaw, pRecord->m_sAnims[ ESides::SIDE_SERVER ].m_flFootYaw ) ) > 5.f ) {
				pRecord->m_iChosenMatrix = 2;
			}
			else if( flSmallestDelta == flMiddlePlaybackDeltaToServer && fabsf( Math::AngleDiff( pRecord->m_sAnims[ ESides::SIDE_MIDDLE ].m_flFootYaw, pRecord->m_sAnims[ ESides::SIDE_SERVER ].m_flFootYaw ) ) > 5.f ) {
				pRecord->m_iChosenMatrix = 3;
			}
		}
	}
	else {
		pRecord->m_sAnims[ ESides::SIDE_OPPOSITEL ].m_bSimulated = false;
		pRecord->m_sAnims[ ESides::SIDE_OPPOSITER ].m_bSimulated = false;
		pRecord->m_sAnims[ ESides::SIDE_MIDDLE ].m_bSimulated = false;
	}

	/*if( pRecord->m_iChosenMatrix == 1 )
		m_pEntity->m_PlayerAnimState( )->m_flFootYaw = pRecord->m_sAnims[ ESides::SIDE_OPPOSITER ].m_flFootYaw;
	else if( pRecord->m_iChosenMatrix == 2 )
		m_pEntity->m_PlayerAnimState( )->m_flFootYaw = pRecord->m_sAnims[ ESides::SIDE_OPPOSITEL ].m_flFootYaw;
	else if( pRecord->m_iChosenMatrix == 3 )
		m_pEntity->m_PlayerAnimState( )->m_flFootYaw = pRecord->m_sAnims[ ESides::SIDE_MIDDLE ].m_flFootYaw;*/

	// run a full animation update
	UpdatePlayer( pRecord );

	pRecord->m_flBodyEyeDelta = Math::AngleDiff( m_pEntity->m_PlayerAnimState( )->m_flFootYaw, pRecord->m_flLowerBodyYawTarget );

	// correct poses if fake angles.

	/*if( !( pRecord->m_fFlags & FL_ONGROUND ) ) {
		m_pEntity->m_flPoseParameter( )[ 2 ] = RandomInt( 0, 4 ) * 0.25f;
		m_pEntity->m_flPoseParameter( )[ 11 ] = RandomInt( 1, 3 ) * 0.25f;
	}*/

	// store updated/animated poses and rotation in lagrecord.	
	std::memcpy( pRecord->m_sAnims[ ESides::SIDE_SERVER ].m_pPoseParameters.data( ), m_pEntity->m_flPoseParameter( ), sizeof( float ) * 20 );
	pRecord->m_sAnims[ ESides::SIDE_SERVER ].m_angAbsAngles = m_pEntity->GetAbsAngles( );

	// restore to our server animation layers
	std::memcpy( m_pEntity->m_AnimOverlay( ).Base( ), pRecord->m_sAnims[ ESides::SIDE_SERVER ].m_pServerAnimOverlays.data( ), 13 * sizeof( C_AnimationLayer ) );

	// restore bone matrix
	std::memcpy( m_pEntity->m_CachedBoneData( ).Base( ), pBackupMatrix, nBoneCount * sizeof( matrix3x4_t ) );

	// restore backup data.
	m_pEntity->m_vecOrigin( ) = m_vecOrigin;
	m_pEntity->m_vecVelocity( ) = m_vecVelocity;
	m_pEntity->SetAbsVelocity( m_vecAbsVelocity );
	m_pEntity->m_fFlags( ) = m_fFlags;
	m_pEntity->m_iEFlags( ) = m_iEFlags;
	m_pEntity->m_flDuckAmount( ) = m_flDuckAmount;
	m_pEntity->m_flLowerBodyYawTarget( ) = m_flLowerBodyYawTarget;
	m_pEntity->SetAbsOrigin( m_vecAbsOrigin );
}

void Animations::AnimationEntry_t::UpdateEntry( C_CSPlayer *pEntity ) {
	if( !pEntity ) {
		return;
	}

	// entity died, invalidate this fker
	if( pEntity->IsDead( ) ) {
		m_pEntity = nullptr;
		return ClearRecords( );
	}

	// entity ptr changed, reset data
	if( m_pEntity != pEntity ) {
		ClearRecords( );

		if( m_flSpawnTime > 0.f )
			m_flSpawnTime = 0.f;
	}

	// update entity pointer, and we can 
	// now determine if we should animate the player
	m_pEntity = pEntity;

	// don't wanna do anything here, but take note
	// that this player entered into dormancy
	if( pEntity->m_flSimulationTime( ) == 0.f || pEntity->IsDormant( ) ) {
		m_bEnteredDormancy = true;

		ClearRecords( );

		// just for sanity sakes
		if( pEntity->EntIndex( ) > 0 && pEntity->EntIndex( ) <= 64 ) {
			auto &resolverData = g_Resolver.m_arrResolverData.at( pEntity->EntIndex( ) );

			resolverData.ResetStageSpecific( EResolverStages::RES_MOVE );
			resolverData.ResetStageSpecific( EResolverStages::RES_AIR );
		}

		return;
	}

	// reset animation state when we detect
	// a respawn for this player
	if( m_flSpawnTime != pEntity->m_flSpawnTime( ) ) {
		auto pState = pEntity->m_PlayerAnimState( );
		if( pState ) {
			pState->m_pPlayer = pEntity;
			pState->Reset( );
		}

		ClearRecords( );

		// update spawntime
		m_flSpawnTime = pEntity->m_flSpawnTime( );
	}

	// clear records, don't wanna shoot bullshit
	bool bLeftDormancy = m_bEnteredDormancy;
	if( m_bEnteredDormancy ) {
		ClearRecords( );
		m_bEnteredDormancy = false;
	}

	// don't store an insane amount of data
	while( m_deqRecords.size( ) > TIME_TO_TICKS( 1.f ) ) {
		m_deqRecords.pop_back( );
	}

	bool bInvalid = m_deqRecords.empty( ) || bLeftDormancy;

	// let's create some sanity checks
	if( !bInvalid ) {
		// somehow the simulation time of the record we previously
		// kept track of is larger than the latest simulation time
		// we received, this means they probably shifted tickbase
		// or something else weird happened.
		if( pEntity->m_flSimulationTime( ) <= pEntity->m_flOldSimulationTime( ) || pEntity->m_flSimulationTime( ) <= m_flOldSimulationTime ) {
			bInvalid = true;
		}

		// if we have any valid records, compare the simulation
		// time against them to see if we received an invalid update.
		if( !m_deqRecords.empty( ) && m_deqRecords.front( ).m_flSimulationTime > 0.f ) {
			if( m_deqRecords.front( ).m_flSimulationTime >= pEntity->m_flSimulationTime( ) ) {
				bInvalid = true;
			}
		}

		if( m_flPreviousLayer11Cycle == -1.f ) {
			// first time set, can't compare yet
			m_flPreviousLayer11Cycle = pEntity->m_AnimOverlay( )[ ANIMATION_LAYER_ALIVELOOP ].m_flCycle;
		}
		else {
			// ANIMATION_LAYER_ALIVELOOP cycle always (!) increments on animation update
			// as we only want to update animations whenever the server also did, we skip this net update
			// as animations haven't updated yet :-)
			if( m_flPreviousLayer11Cycle == pEntity->m_AnimOverlay( )[ ANIMATION_LAYER_ALIVELOOP ].m_flCycle ) {
				// mark as invalid
				bInvalid = true;

				// update these so we don't add a record with wrong origin etc...
				m_flOldSimulationTime = pEntity->m_flSimulationTime( );
			}

			m_flPreviousLayer11Cycle = pEntity->m_AnimOverlay( )[ ANIMATION_LAYER_ALIVELOOP ].m_flCycle;
		}
	}

	// only allow simulation time updates
	if( pEntity->m_flSimulationTime( ) <= m_flOldSimulationTime )
		return;

	// fill this record with data, it will be updated/corrected
	// when we run an animation update on it
	auto pRecord = &m_deqRecords.emplace_front( );
	pRecord->SetupRecord( pEntity, false );

	// remove changes to layers made by SetupLean
	// these don't appear on the server
	pEntity->m_AnimOverlay( )[ ANIMATION_LAYER_LEAN ].m_flWeight = 0.f;

	// update server animation layers
	std::memcpy( pRecord->m_sAnims[ ESides::SIDE_SERVER ].m_pServerAnimOverlays.data( ), pEntity->m_AnimOverlay( ).Base( ), 13 * sizeof( C_AnimationLayer ) );

	// tell cheat this record is INVALID.
	pRecord->m_bInvalid = bInvalid;

	// run animation update on this record
	// resolve players, fix animations, etc
	UpdateAnimations( pRecord );

	// generate an aimbot matrix using the server bone setup with server layers :-)
	pRecord->m_sAnims[ ESides::SIDE_SERVER ].m_pBoneBuilder.store( pRecord, BONE_USED_BY_SERVER );
	pRecord->m_sAnims[ ESides::SIDE_SERVER ].m_pBoneBuilder.setup( );

	// copy over our aimbot matrix to our visual matrix
	std::memcpy( pRecord->m_pVisualMatrix, pRecord->m_sAnims[ ESides::SIDE_SERVER ].m_pMatrix, sizeof( matrix3x4_t ) * MAXSTUDIOBONES );

	// get rid of the origin in the matrix
	for( auto i = 0; i < MAXSTUDIOBONES; i++ ) {
		pRecord->m_pVisualMatrix[ i ].MatrixSetColumn( pRecord->m_pVisualMatrix[ i ].MatrixGetColumn( 3 ) - pRecord->m_vecPredOrigin, 3 );
	}
}

void Animations::OnFrameStageNotify( ) {
	if( !g_pEngine->IsInGame( ) || !g_pEngine->IsConnected( ) ) {
		for( int i = 1; i <= 64; ++i ) {
			m_uAnimationEntry[ i ].ClearRecords( );
			m_uAnimationEntry[ i ].m_pEntity = nullptr;
		}

		return;
	}

	UpdateLerpTime( );
	UpdateLatency( );

	// update animations for all players
	for( int i = 1; i <= g_pGlobalVars->maxClients; ++i ) {
		const auto pEntity = C_CSPlayer::GetPlayerByIndex( i );
		if( !pEntity ) {
			continue;
		}

		if( i == g_pEngine->GetLocalPlayer( ) ) {
			continue;
		}

		m_uAnimationEntry.at( i ).UpdateEntry( pEntity );
	}
}

void Animations::UpdatePlayerSimple( C_CSPlayer *pEntity ) {
	if( !pEntity )
		return;

	LagRecord_t dummy;
	dummy.m_pEntity = pEntity;
	dummy.m_flAnimationTime = pEntity->m_flOldSimulationTime( ) + g_pGlobalVars->interval_per_tick;

	m_uAnimationEntry.at( pEntity->EntIndex( ) ).UpdatePlayer( &dummy );
}

LagRecord_t Animations::GetLatestRecord( int nEntIndex, bool bValidCheck ) {
	auto pEntry = GetAnimationEntry( nEntIndex );
	if( !pEntry || pEntry->m_deqRecords.empty( ) )
		return { };

	for( auto it = pEntry->m_deqRecords.begin( ); it != pEntry->m_deqRecords.end( ); it = next( it ) ) {
		if( bValidCheck ) {
			if( !it->IsRecordValid( ) || it->m_bInvalid || ( it->m_bBrokeTeleportDst && it != pEntry->m_deqRecords.begin( ) ) )
				continue;
		}

		return *it;
	}

	return { };
}

LagRecord_t Animations::GetOldestRecord( int nEntIndex, bool bValidCheck ) {
	auto pEntry = GetAnimationEntry( nEntIndex );
	if( !pEntry || pEntry->m_deqRecords.empty( ) )
		return { };

	for( auto it = pEntry->m_deqRecords.rbegin( ); it != pEntry->m_deqRecords.rend( ); it = next( it ) ) {
		if( bValidCheck ) {
			if( !it->IsRecordValid( ) || it->m_bInvalid || ( it->m_bBrokeTeleportDst && it != pEntry->m_deqRecords.rend( ) ) )
				continue;
		}

		return *it;
	}

	return { };
}

bool Animations::GetVisualMatrix( C_CSPlayer *entity, matrix3x4_t *out, bool bBodyUpdate ) {
	if( !entity )
		return false;

	if( g_Vars.cl_lagcompensation->GetInt( ) == 0 )
		return false;

	auto data = GetAnimationEntry( entity->m_entIndex );
	if( data ) {
		LagRecord_t pTrack{ };
		LagRecord_t *pTrackLast = nullptr;

		if( bBodyUpdate ) {
			for( auto it = data->m_deqRecords.rbegin( ); it != data->m_deqRecords.rend( ); ++it ) {
				if( it->m_pEntity != entity )
					break;

				if( it->m_bExtrapolated )
					continue;

				if( /*it->m_bIsInvalid ||*/ it->m_bBrokeTeleportDst )
					continue;

				if( !it->IsRecordValid( ) )
					continue;

				if( bBodyUpdate && !it->m_bLBYFlicked )
					continue;

				if( bBodyUpdate ) {
					std::memcpy( out, it->m_sAnims[ ESides::SIDE_SERVER ].m_pMatrix, sizeof( matrix3x4_t[ MAXSTUDIOBONES ] ) );
					g_Models.m_flLeAlpha.at( entity->EntIndex( ) ) = 1.f;
					return true;
				}

				std::memcpy( out, it->m_sAnims[ ESides::SIDE_SERVER ].m_pMatrix, sizeof( matrix3x4_t[ MAXSTUDIOBONES ] ) );
				return true;
			}
		}
		else {
			// start from begin
			for( auto it = data->m_deqRecords.begin( ); it != data->m_deqRecords.end( ); ++it ) {
				if( it->m_pEntity != entity )
					break;

				if( it->m_bExtrapolated || it->m_bBrokeTeleportDst )
					return false;

				std::pair< LagRecord_t *, LagRecord_t * > last;
				if( it->IsRecordValid( ) && ( it + 1 ) != data->m_deqRecords.end( ) && !( it + 1 )->IsRecordValid( ) )
					last = std::make_pair( &*( it + 1 ), &*it );

				if( !last.first || !last.second )
					continue;

				const auto &pFirstInvalid = last.first;
				const auto &pLastInvalid = last.second;

				if( !pLastInvalid || !pFirstInvalid )
					continue;

				if( pLastInvalid->m_flSimulationTime - pFirstInvalid->m_flSimulationTime > 0.5f )
					continue;

				if( ( pFirstInvalid->m_vecOrigin - entity->m_vecOrigin( ) ).Length( ) < 1.f )
					continue;

				const auto NextOrigin = pLastInvalid->m_vecOrigin;
				const auto curtime = g_pGlobalVars->curtime;

				auto flDelta = 1.f - ( curtime - pLastInvalid->m_flInterpolateTime ) / ( pLastInvalid->m_flSimulationTime - pFirstInvalid->m_flSimulationTime );
				if( flDelta < 0.f || flDelta > 1.f )
					pLastInvalid->m_flInterpolateTime = curtime;

				flDelta = 1.f - ( curtime - pLastInvalid->m_flInterpolateTime ) / ( pLastInvalid->m_flSimulationTime - pFirstInvalid->m_flSimulationTime );

				const auto lerp = Math::Interpolate( NextOrigin, pFirstInvalid->m_vecOrigin, std::clamp( flDelta, 0.f, 1.f ) );

				matrix3x4_t ret[ 128 ];
				auto pMatrix = pFirstInvalid->m_sAnims[ ESides::SIDE_SERVER ].m_pMatrix;
				memcpy( ret, pMatrix, sizeof( matrix3x4_t[ 128 ] ) );

				for( size_t i{ }; i < 128; ++i ) {
					const auto matrix_delta = Math::MatrixGetOrigin( pMatrix[ i ] ) - pFirstInvalid->m_vecOrigin;
					Math::MatrixSetOrigin( matrix_delta + lerp, ret[ i ] );
				}

				memcpy( out, ret, sizeof( matrix3x4_t[ 128 ] ) );
				return true;
			}
		}
	}

	return false;
}

Animations g_Animations;