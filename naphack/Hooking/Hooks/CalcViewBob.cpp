#include "../../pandora.hpp"
#include "../Hooked.hpp"
#include "../../SDK/Displacement.hpp"
#include "../../SDK/Classes/Player.hpp"

void __fastcall Hooked::CalcViewBob( C_BasePlayer* player, void* edx, Vector& eyeOrigin ) {
	return;
}

void __fastcall Hooked::CalcView( C_CSPlayer *pPlayer, void *edx, Vector &vecEyeOrigin, QAngle &angEyeAngles, float &flZNear, float &flZFar, float &flFov ) {
	auto pEntity = ( C_CSPlayer * )pPlayer;
	if( pEntity->EntIndex( ) != g_pEngine->GetLocalPlayer( ) )
		return oCalcView( pPlayer, vecEyeOrigin, angEyeAngles, flZNear, flZFar, flFov );

	// prevent client's ModifyEyePos from being called
	// https://i.imgur.com/LMYq6Jf.png

	const bool m_bUseNewAnimstate = *( int * )( ( uintptr_t )pPlayer + 0x39E1 );
	*( bool * )( ( uintptr_t )pPlayer + 0x39E1 ) = false;
	oCalcView( pPlayer, vecEyeOrigin, angEyeAngles, flZNear, flZFar, flFov );
	*( bool * )( ( uintptr_t )pPlayer + 0x39E1 ) = m_bUseNewAnimstate;
}