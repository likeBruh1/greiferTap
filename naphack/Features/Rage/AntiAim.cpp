#include "AntiAim.hpp"

#include "../Miscellaneous/Movement.hpp"
#include "../Rage/FakeLag.hpp"
#include "../Visuals/Visuals.hpp"
#include "ServerAnimations.hpp"
#include "EnginePrediction.hpp"
#include "TickbaseShift.hpp"
#include "../../SDK/Classes/Player.hpp"
#include "../../SDK/Classes/weapon.hpp"
#include "Autowall.hpp"
#include "../Visuals/EventLogger.hpp"
#include "../Miscellaneous/PlayerList.hpp"

AntiAim g_AntiAim;

// #define DMG_BASED_FREESTAND

C_CSPlayer *AntiAim::GetBestPlayer( bool distance ) {
	const auto pLocal = C_CSPlayer::GetLocalPlayer( );
	if( !pLocal )
		return false;

	float flBestDistance = FLT_MAX;
	float flBestFoV = FLT_MAX;
	C_CSPlayer *pBestPlayer = nullptr;

	QAngle angViewAngle;
	g_pEngine->GetViewAngles( angViewAngle );

	for( int i = 1; i <= g_pGlobalVars->maxClients; i++ ) {
		auto player = C_CSPlayer::GetPlayerByIndex( i );
		if( !player || player->IsDead( ) )
			continue;

		if( player->IsDormant( ) )
			continue;

		if( player->IsTeammate( pLocal ) )
			continue;

		if( g_PlayerList.GetSettings( player->GetSteamID( ) ).m_bAddToWhitelist && !g_Vars.misc.force_ignore_whitelist.enabled )
			continue;

		if( distance ) {
			float flDistance = ( player->m_vecOrigin( ) - pLocal->m_vecOrigin( ) ).LengthSquared( );
			if( flDistance < flBestDistance ) {
				flBestDistance = flDistance;
				pBestPlayer = player;
			}
		}
		else {
			float flFoV = Math::GetFov( angViewAngle, pLocal->GetEyePosition( ), player->m_vecOrigin( ) );

			if( flFoV < flBestFoV ) {
				flBestFoV = flFoV;
				pBestPlayer = player;
			}
		}
	}

	return pBestPlayer;
}

void AntiAim::Think( CUserCmd *pCmd, bool *bSendPacket, bool *bFinalPacket ) {
	bool attack, attack2;
	if( !g_Vars.rage.anti_aim_active )
		return;

	const auto pLocal = C_CSPlayer::GetLocalPlayer( );
	if( !pLocal )
		return;

	auto pWeapon = reinterpret_cast< C_WeaponCSBaseGun * >( pLocal->m_hActiveWeapon( ).Get( ) );
	if( !pWeapon )
		return;

	auto pWeaponData = pWeapon->GetCSWeaponData( );
	if( !pWeaponData.IsValid( ) )
		return;

	attack = pCmd->buttons & IN_ATTACK;
	attack2 = pCmd->buttons & IN_ATTACK2;

	// disable on round freeze period.
	if( g_pGameRules.Xor( ) ) {
		if( g_pGameRules->m_bFreezePeriod( ) )
			return;
	}

	// disable if moving around on a ladder.
	if( g_Vars.globals.m_bGameOver || ( ( pCmd->forwardmove || pCmd->sidemove ) && ( pLocal->m_MoveType( ) == MOVETYPE_LADDER ) || pLocal->m_MoveType( ) == MOVETYPE_NOCLIP ) )
		return;

	// disable if shooting.
	if( attack && pLocal->CanShoot( ) && pWeaponData->m_iWeaponType != WEAPONTYPE_GRENADE )
		return;

	// CBaseCSGrenade::ItemPostFrame()
	// https://github.com/VSES/SourceEngine2007/blob/master/src_main/game/shared/cstrike/weapon_basecsgrenade.cpp#L209
	if( pWeaponData->m_iWeaponType == WEAPONTYPE_GRENADE && pWeapon->m_fThrowTime( ) > 0.f )
		return;

	// disable if knifing.
	if( attack2 && pWeaponData->m_iWeaponType == WEAPONTYPE_KNIFE && pLocal->CanShoot( ) )
		return;

	QAngle vecCurView;
	g_pEngine->GetViewAngles(vecCurView);

	float manualAngle = vecCurView.y;

	switch (m_eSide) {
	case SIDE_LEFT:
		manualAngle += 90.f;
		break;
	case SIDE_RIGHT:
		manualAngle -= 90.f;
		break;
	case SIDE_BACK:
		manualAngle += 180.f;
		break;
	}

	/* we do the body breaking. */
	if (DoTheBreaking(pCmd, bSendPacket, bFinalPacket)) {
		return;
	}

	/* do the yaw adjustments. */
	pCmd->viewangles.y = manualAngle;

	/* normalize angles in order to not get kicked. */
	pCmd->viewangles.y = Math::AngleNormalize( pCmd->viewangles.y );
}

bool AntiAim::DoTheBreaking(CUserCmd* pCmd, bool* bSendPacket, bool* bFinalPacket) {
	const auto pLocal = C_CSPlayer::GetLocalPlayer();
	if (!pLocal)
		return false;

	auto pState = pLocal->m_PlayerAnimState();
	if (!pState)
		return false;

	const float preflickTime = TICKS_TO_TIME(4);

	float bodyYaw = g_ServerAnimations.m_uServerAnimations.m_flLowerBodyYawTarget;
	float bodyYawTimer = g_ServerAnimations.m_uServerAnimations.m_flLowerBodyRealignTimer;

	/* we are not on ground yet*/
	if (!pState->m_bOnGround) {
		/* do that preflick ish to ensure that we break on land Bruh. */
		if (pLocal->m_fFlags() & FL_ONGROUND) {
			/* choose where we want to break lby. */
			m_flDesiredBodyYaw = bodyYaw;
			pCmd->viewangles.y = bodyYaw + 180.f;
			return true;
		}

		return false;
	}

	if (g_pGlobalVars->curtime > bodyYawTimer) {
		/* break that hoe. */
		pCmd->viewangles.y = m_flDesiredBodyYaw;
		return true;
	}

	/* fuck that fakelag shit Bruh. */
	if ((g_pGlobalVars->curtime + preflickTime) > bodyYawTimer) {
		/* force 1 choke bruh. */
		*bSendPacket = g_pClientState->m_nChokedCommands() >= 1;

		/* choose where we want to break lby. */
		m_flDesiredBodyYaw = bodyYaw;

		/* now we do that preflick ish. */
		pCmd->viewangles.y = bodyYaw + 180.f;
		return true;
	}

	return false;
}