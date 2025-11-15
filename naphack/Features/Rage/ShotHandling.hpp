#pragma once
#include "Animations.hpp"
#include <vector>
#include <deque>

#include "Ragebot.hpp"
struct ShotInformation_t {
	//AimPoint_t m_pBestPoint;
	//AimTarget_t m_pTarget;

	int m_iTargetIndex;
	int m_iEnemyHealth;
	int m_iPredictedDamage;
	int m_iMinimalDamage;
	int m_iHitchance;
	int m_iTraceHitgroup;
	int m_iHistoryTicks;
	int m_iShotID;
	float m_flTime;

	LagRecord_t m_pRecord;

	std::string m_szFiredLog;

	bool m_bLogged = false;

	bool m_bTapShot = false;
	bool m_bMatched = false;
	bool m_bHadPredError = false;

	std::string m_szName = "";
	std::string m_szFlags;

	mstudiobbox_t* m_pHitbox = nullptr;

	Vector m_vecStart;
	Vector m_vecEnd;
};

struct ShotEvents_t {
	struct player_hurt_t {
		int m_iDamage;
		int m_iHealth;
		int m_iHitgroup;
		int m_iTargetIndex;
	};

	std::vector<Vector> m_vecImpacts;
	std::vector<player_hurt_t> m_vecDamage;

	ShotInformation_t m_ShotData;
};

class ShotHandling {
public:
	std::string HitgroupToString( int hitgroup );
	void ProcessShots( );
	void RegisterShot( ShotInformation_t& shot );
	void GameEvent( IGameEvent* pEvent );

	bool TraceShot( ShotEvents_t* shot );

	std::deque<ShotEvents_t> m_vecFilteredShots;
	std::deque<ShotEvents_t> m_vecShots;
};

extern ShotHandling g_ShotHandling;