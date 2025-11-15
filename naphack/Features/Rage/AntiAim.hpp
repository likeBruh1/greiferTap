#pragma once
#include "../../SDK/sdk.hpp"

class AdaptiveAngle {
public:
	float m_yaw;
	float m_dist;

public:
	// ctor.
	__forceinline AdaptiveAngle( float yaw, float penalty = 0.f ) {
		// set yaw.
		m_yaw = Math::AngleNormalize( yaw );

		// init distance.
		m_dist = 0.f;

		// remove penalty.
		m_dist -= penalty;
	}
};

enum ESide {
	SIDE_LEFT,
	SIDE_RIGHT,
	SIDE_BACK,
	SIDE_FWD,
	SIDE_MAX
};

class AntiAim {
public:
	void Think( CUserCmd *pCmd, bool *bSendPacket, bool *bFinalPacket );
	ESide GetSide( ) { return m_eSide; }

	bool DoEdgeAntiAim( C_CSPlayer *player, QAngle &out );
	C_CSPlayer *GetBestPlayer( bool distance = false );
	float UpdateFreestandPriority( float flLength, int nIndex, bool bDogshit = false );
	bool IsAbleToFlick( C_CSPlayer *pLocal );
private:
	void DoFakeYaw( CUserCmd *pCmd, C_CSPlayer *pLocal );
	void DoRealYaw( CUserCmd *pCmd, C_CSPlayer *pLocal );
	void DoPitch( CUserCmd *pCmd, C_CSPlayer *pLocal );
	void AutoDirection( C_CSPlayer *pLocal );
	void PerformBodyFlick( CUserCmd *pCmd, bool *bSendPacket, float flOffset = 0.f );
	void HandleManual( CUserCmd *pCmd, C_CSPlayer *pLocal );
	void DesyncLastMove( CUserCmd *pCmd, bool *bSendPacket );
private:
	float m_flLastRealAngle;

	ESide m_eAutoSide;
	float m_flAutoYaw;
	float m_flAutoDist;
	float m_flAutoTime;
	int  m_nBodyFlicks;
	bool m_bJitterUpdate;

	float m_flRandDistortFactor;
	float m_flRandDistortSpeed;

	ESide m_eBreakerSide = SIDE_MAX;
	ESide m_eSide = SIDE_MAX;
public:
	bool m_bEdging;
	bool m_bHasValidAuto;

	float m_flEdgeOrAutoYaw = FLT_MAX;
	bool m_bHidingLBYFlick = false;
	bool m_bDistorting = false;
	bool m_bWaitForBreaker = false;

	float m_flLastPrebrakeAngle;

	bool m_bLastPacket;
	bool m_bHasOverriden;
	float m_flAngleToAdd;
	bool m_bWasHit;

	bool m_bLbyUpdateThisTick;
	QAngle angViewangle;
	float  flLastMovingLby;
	bool m_bAllowFakeWalkFlick;

	bool m_bLandDesynced = false;
};

extern AntiAim g_AntiAim;