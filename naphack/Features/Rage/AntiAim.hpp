#pragma once
#include "../../SDK/sdk.hpp"

class AdaptiveAngle {
public:
	float m_yaw;
	float m_dist;

public:
	// ctor.
	__forceinline AdaptiveAngle(float yaw, float penalty = 0.f) {
		// set yaw.
		m_yaw = Math::AngleNormalize(yaw);

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
	C_CSPlayer *GetBestPlayer( bool distance = false );

	ESide GetSide() { return m_eSide; }
	float GetDesiredBodyYaw() { return m_flDesiredBodyYaw; }
private:
	ESide m_eSide = SIDE_MAX;
	float m_flDesiredBodyYaw;
public:
	/* doot like Bruh. */
	bool DoTheBreaking(CUserCmd* pCmd, bool* bSendPacket, bool* bFinalPacket);
};

extern AntiAim g_AntiAim;