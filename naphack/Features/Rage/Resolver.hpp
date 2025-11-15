#pragma once
#include "Animations.hpp"
#include <vector>
#include <deque>

enum EResolverStages : int {
	RES_NONE,
	RES_STAND,
	RES_MOVE,
	RES_AIR,
	RES_OVERRIDE,
	RES_MAX
};

enum EBodyState : int {
	BODY_DEFAULT,
	BODY_BREAK
};

enum EConfidence : int {
	CONF_LOW,
	CONF_MED,
	CONF_HIGH,
	CONF_VHIGH
};

struct ResolverData_t {

	__forceinline bool IsValidFloat( float fl ) {
		return fl != -1.f;
	}

	__forceinline bool IsValidInt( int n ) {
		return n != -1;
	}

	__forceinline void Reset( ) {
		// reset every stage
		for( int i = EResolverStages::RES_STAND; i < EResolverStages::RES_MAX; ++i ) {
			ResetStageSpecific( ( EResolverStages )i );
			m_nMissedShots[ i ] = 0;
		}
	}

	// resets jump related data
	__forceinline void ResetStageSpecific( EResolverStages stage ) {
		if( stage <= EResolverStages::RES_NONE ||
			stage >= EResolverStages::RES_MAX )
			return;

		// reset jump related data
		if( stage == EResolverStages::RES_AIR ) {
			m_bPlausibleBody = false;
			return;
		}

		// reset move related data
		if( stage == EResolverStages::RES_MOVE ) {
			m_flLastMovingBody = -1.f;
			m_flLastMovingTime = -1.f;
			m_pMoveData = {};
			return;
		}

		// reset stand data
		m_flLastSafeBody = -1.f;
		m_flNextBodyUpdate = -1.f;
		m_flFreestandYaw = -1.f;
		m_flEdgeYaw = -1.f;
		m_bPredictingBody = false;
		m_bAdjusting = false;
		m_vecDeductedAngles.clear( );
		m_vecPlayerHurtInfo.clear( );
		m_vecBloodAngles.clear( );
		m_eIdealConfidence = EConfidence::CONF_LOW;
		m_eAngleConfidence = EConfidence::CONF_LOW;
		m_flApproxDelta = -1.f;
	
		// DON'T RESET IT HERE
		// m_bMissedFreestand = false;
		// m_nMissedBody = -1;
	}

	// stand specific data
#pragma region STAND_DATA
	struct DeductedAngle_t {
		float m_flAngle;
		float m_flTime;
	};

	struct BloodAngle_t {
		float m_flAngle;
		float m_flTime;
		EConfidence m_eConfidence;
	};

	struct PlayerHurtInfo_t {
		int m_nHitgroup;
		float m_flTime;
	};

	float m_flFreestandYaw = -1.f;
	float m_flEdgeYaw = -1.f;
	float m_flNextBodyUpdate = -1.f;
	bool m_bPredictingBody = false;
	bool m_bMissedFreestand = false;
	bool m_bHasBeenStatic = false;
	bool m_bHasUpdatedLBY = false;
	int m_nMissedBody = -1;
	std::vector<float> m_vecAngles;
	std::vector<DeductedAngle_t> m_vecDeductedAngles;
	std::vector<BloodAngle_t> m_vecBloodAngles;

	EBodyState m_eBodyState;
	bool m_bAdjusting;
	float m_flLastWeightAdjustTime;
	float m_flLastCycleAdjustTime;
	float m_flLastSafeBody;
	EConfidence m_eIdealConfidence;
	EConfidence m_eAngleConfidence;

	float m_flApproxDelta;

	// blood shit
	std::vector<PlayerHurtInfo_t> m_vecPlayerHurtInfo;

#pragma endregion

	// move specific data
#pragma region MOVE_DATA
	LagRecord_t m_pMoveData;

	float m_flLastMovingBody = -1.f;
	float m_flLastMovingTime = -1.f;
#pragma endregion

	// jump specific data
#pragma region JUMP_DATA
	bool m_bPlausibleBody = false;
#pragma endregion

public:
	// count missed shots separately for each resolver stage
	int m_nMissedShots[ EResolverStages::RES_MAX ];
	__forceinline int GetMissedShots( EResolverStages stage ) {
		return m_nMissedShots[ stage ];
	}

	// excluded: this is the current stage we're in,
	// means we will NOT reset missed shots for this stage
	__forceinline void UpdateMissedShots( EResolverStages excluded ) {
		for( int i = 0; i < EResolverStages::RES_MAX; ++i ) {
			if( i == excluded )
				continue;

			m_nMissedShots[ i ] = 0;
		}
	}

	__forceinline void IncrementMissedShots( EResolverStages stage ) {
		++m_nMissedShots[ stage ];
	}

	long long m_ulSteamID = NULL;
};

struct ImpactInfo_t {
	Vector m_vecStart;
	Vector m_vecEnd;

	int m_iExpectedHitgroup;
	bool m_bDealtDamage;
};

class C_TEEffectDispatch;
class Resolver {
	EResolverStages UpdateResolverStage( LagRecord_t *pRecord );

	void OnPlayerResolve( LagRecord_t *pRecord, LagRecord_t *pPreviousRecord );

	void OnPlayerStand( LagRecord_t *pRecord, LagRecord_t *pPreviousRecord );
	void OnPlayerStandTrial( LagRecord_t *pRecord, LagRecord_t *pPreviousRecord );
	void OnPlayerMove( LagRecord_t *pRecord, LagRecord_t *pPreviousRecord );
	void OnPlayerJump( LagRecord_t *pRecord, LagRecord_t *pPreviousRecord );
	void OnPlayerOverride( LagRecord_t *pRecord, LagRecord_t *pPreviousRecord );

	float GetRelativePitch( LagRecord_t *pRecord, float flAngle );
	float GetRelativeYaw( LagRecord_t *pRecord, float flAngle );
	float GetFreestandYaw( LagRecord_t *pRecord );
	void GetApproximateBodyState( LagRecord_t *pRecord, LagRecord_t *pPrevious );
	void GetApproximateBodyDelta( LagRecord_t *pRecord, LagRecord_t *pPrevious );

public:
	__forceinline ResolverData_t &GetResolverData( int n ) {
		if( n <= 0 || n >= 65 ) {
			ResolverData_t bitch;
			bitch.Reset( );

			return bitch;
		}

		return m_arrResolverData.at( n );
	}

	void OnBulletImpact( LagRecord_t *pRecord, ImpactInfo_t *info );
	void OnSpawnBlood( C_TEEffectDispatch *pBlood );

	void CorrectShotRecord( LagRecord_t *record );

	LagRecord_t FindIdealRecord( Animations::AnimationEntry_t *data, float flTargetDamage = 1337.f );

	void ResolvePlayers( LagRecord_t *pRecord, LagRecord_t *pPreviousRecord );
	std::array< ResolverData_t, 65 > m_arrResolverData;
};

extern Resolver g_Resolver;