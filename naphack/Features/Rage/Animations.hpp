#pragma once

#include "../../SDK/sdk.hpp"
#include "../../SDK/Displacement.hpp"

#include "../../SDK/Classes/Player.hpp"
#include "../../SDK/Classes/weapon.hpp"
#include "../../SDK/Valve/CBaseHandle.hpp"

#include "../../Utils/Threading/shared_mutex.h"
#include "../../Utils/Threading/mutex.h"

#include "BoneSetup.hpp"

#include <map>
#include <deque>

enum EResolverStages;
enum EConfidence;
class c_bone_builder;

struct SideInfo_t {
	c_bone_builder m_pBoneBuilder;
	alignas( 16 ) matrix3x4_t m_pMatrix[ 128 ];

	std::array<C_AnimationLayer, 13> m_pServerAnimOverlays;
	std::array<float, 20> m_pPoseParameters;

	QAngle m_angAbsAngles;

	// custom side data
	bool m_bUpdated;
	bool m_bSimulated;
	float m_flFootYaw;

	CCSGOPlayerAnimState m_pAnimState;
};

struct HitboxPosition_t {
	int nHitboxIndex;
	Vector vecPosition;
};

enum ESides {
	SIDE_SERVER,
	SIDE_OPPOSITEL,
	SIDE_OPPOSITER,
	SIDE_MIDDLE ,
	SIDE_INVALID
};

struct LagRecord_t {
	C_CSPlayer *m_pEntity;
	int m_nEntIndex;

	// netvars
	float m_flSimulationTime;
	float m_flLowerBodyYawTarget;
	float m_flDuckAmount;
	float m_fLastShotTime;
	float m_flDurationInAir;

	bool m_bGunGameImmunity;

	int m_iChosenMatrix = 0;

	int m_fFlags;

	Vector m_vecMins;
	Vector m_vecMaxs;

	Vector m_vecOrigin;
	Vector m_vecAbsOrigin;

	Vector m_vecVelocity;
	Vector m_vecAbsVelocity;

	QAngle m_angEyeAngles;

	C_WeaponCSBaseGun *m_pWeapon;

	SideInfo_t m_sAnims[ ESides::SIDE_INVALID ];
	
	alignas( 16 ) matrix3x4_t m_pVisualMatrix[ 128 ];
	alignas( 16 ) matrix3x4_t m_pBackupMatrix[ 128 ];

	// record specific
	int m_fPredFlags;
	float m_flPredSimulationTime;
	Vector m_vecPredOrigin;
	Vector m_vecPredVelocity;
	int m_nIdeality;
	float m_flBodyEyeDelta;

	int m_nPredictedTicks = 0;

	int m_nServerTick;

	Vector m_vecOriginDelta;

	bool m_bBrokeTeleportDst;
	bool m_bExtrapolated;
	bool m_bInvalid = false;
	bool m_bShiftingTickbase;
	bool m_bDelaying = false;

	int m_nChokedTicks;
	int m_nServerDeltaTicks;

	bool m_bSimTick = false;
	bool m_bFixingPitch;
	bool m_bShooting;

	float m_flCreationTime;

	float m_flChokedTime;
	float m_flAnimationTime;
	float m_flInterpolateTime;

	float m_flLastNonShotPitch;

	EResolverStages m_eResolverStage;
	EConfidence m_eConfidence;
	int m_iResolverType;
	std::string m_szResolver = "invalid";

	bool m_bIsResolved;
	bool m_bLBYFlicked;

	bool m_bPotentialDesync;

	Vector m_vecEyePosition;
	Vector m_vecLowerChestPosition;
	Vector m_vecChestPosition;
	Vector m_vecPelvisPosition;
	Vector m_vecStomachPosition;

	// aimbot specific
	mstudiobbox_t *m_bbox;

	// record func stuff
	bool m_bIsBackup;

	void SetupRecord( C_CSPlayer *pEntity, bool bBackup = false );
	void ApplyRecord( C_CSPlayer *pEntity );
	bool IsRecordValid( bool bSkipDeadTime = false );

	__forceinline void Predict( ) {
		m_vecPredOrigin = m_vecOrigin;
		m_vecPredVelocity = m_vecVelocity;
		m_flPredSimulationTime = m_flSimulationTime;
		m_fPredFlags = m_fFlags;
	}
};

enum HitscanMode : int {
	NORMAL = 0,
	LETHAL = 1,
	LETHAL2 = 3,
	PREFER = 4
};

struct HitscanData_t {
	float  m_damage;
	Vector m_pos;

	__forceinline HitscanData_t( ) : m_damage{ 0.f }, m_pos{}{}
};

struct HitscanBox_t {
	int         m_index;
	HitscanMode m_mode;

	__forceinline bool operator==( const HitscanBox_t &c ) const {
		return m_index == c.m_index && m_mode == c.m_mode;
	}
};

class Animations {
public:
	struct AnimationEntry_t {
	private:
		bool m_bEnteredDormancy;
		float m_flSpawnTime;

		float m_flOldSimulationTime = 0.f;

		void SimulateSideAnimation( LagRecord_t *pRecord, LagRecord_t *pPrevious, ESides eSide, float flTargetAbs );
		void UpdateAnimations( LagRecord_t *pRecord );
	public:

		float m_flPreviousLayer11Cycle = -1.f;
		float m_flPreviousLayer11CyclePostDataUpdate = -1.f;
		float m_flOldSimulationTimeAlt = 0.f;

		int m_nServerTick = 0;

		__forceinline void ClearRecords( ) {
			if( m_deqRecords.empty( ) )
				return;

			m_deqRecords.clear( );

			m_flOldSimulationTime = 0.f;
			m_flPreviousLayer11Cycle = -1.f;
		}

		void UpdatePlayer( LagRecord_t *pRecord );

		C_CSPlayer *m_pEntity;
		std::deque<LagRecord_t> m_deqRecords;

		LagRecord_t m_backupRecord;

		void UpdateEntry( C_CSPlayer *pEntity );

		// aimbot specific
		void SetupHitboxes( LagRecord_t *record, bool history );
		bool SetupHitboxPoints( LagRecord_t *record, matrix3x4_t *bones, int index, std::vector< std::pair<mstudiobbox_t *, std::pair<Vector, bool>> > &points );
		bool GetBestAimPosition( Vector &aim, float &damage, LagRecord_t *record );

		using hitboxcan_t = stdpp::unique_vector< HitscanBox_t >;
		hitboxcan_t m_hitboxes;

		bool m_bDelay;

		__forceinline void reset( ) {
			m_pEntity = nullptr;
			m_flSpawnTime = 0.f;

			m_deqRecords.clear( );
			m_hitboxes.clear( );
		}
	};

private:
	std::array< AnimationEntry_t, 65 > m_uAnimationEntry;
public:
	void OnFrameStageNotify( );

	LagRecord_t GetLatestRecord( int nEntIndex, bool bValidCheck = true );
	LagRecord_t GetOldestRecord( int nEntIndex, bool bValidCheck = true );

	bool GetVisualMatrix( C_CSPlayer *pEntity, matrix3x4_t *pMatrix, bool bBodyUpdate );

	void UpdatePlayerSimple( C_CSPlayer* pEntity );

	__forceinline AnimationEntry_t *GetAnimationEntry( int nEntIndex ) {
		return &m_uAnimationEntry.at( nEntIndex );
	}

	__forceinline bool BreakingTeleportDistance( int nEntIndex ) {
		auto pAnimEntry = GetAnimationEntry( nEntIndex );
		if( !pAnimEntry )
			return false;

		if( pAnimEntry->m_deqRecords.empty( ) )
			return false;

		for( auto &record : pAnimEntry->m_deqRecords ) {
			if( !record.IsRecordValid( ) )
				continue;

			// return out of function, anyway
			// this invalidates the whole track
			if( record.m_bBrokeTeleportDst ) {
				return true;
			}
		}

		return false;
	}

	float m_flOutgoingLatency;
	float m_flIncomingLatency;
	float m_fLerpTime;
	int m_nArrivalTick;

private:
	void UpdateLerpTime( );
	void UpdateLatency( );
};

extern Animations g_Animations;