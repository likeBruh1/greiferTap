#pragma once
#include "../../SDK/sdk.hpp"
#include "../Rage/Animations.hpp"
#include <optional>

class ServerAnimations {
	struct AnimationInfo_t {
		std::array<C_AnimationLayer, 13> m_pAnimOverlays;
		std::array<float, 20> m_pPoseParameters;

		alignas(16) Vector m_vecBonePos[ 128 ];
		alignas( 16 ) Quaternion m_quatBoneRot[ 128 ];

		float m_flFootYaw;
		float m_flEyeYaw;
		float m_flLowerBodyYawTarget;
		float m_flLowerBodyRealignTimer;
		float m_flSpawnTime;

		bool m_bBreakingTeleportDst;
		Vector m_vecLastOrigin;

		CCSGOPlayerAnimState m_pAnimState;
		bool m_bInitializedAnimState;

		QAngle m_angUpdateAngles;

		bool m_bRealignBreaker;

		bool m_bFirstFlick = false;
		bool m_bWaitForBreaker = false;

		bool m_bDoingPreFlick = false;
		bool m_bDoingRealFlick = false;

		float m_flBodyAlpha;

		bool m_bSetupBones;
		alignas( 16 ) matrix3x4_t m_pMatrix[ MAXSTUDIOBONES ];
	};

	void SetLayerInactive( C_AnimationLayer *pLayer, int idx );

	void SetLayerSequence( C_CSPlayer *pEntity, C_AnimationLayer *pLayer, int32_t activity, /*CUtlVector<uint16_t> modifiers,*/ int nOverrideSequence = -1 );

	QAngle angThirdPersonAngles;

	bool m_bCompute = false;

public:
	bool HandleLayerSeparately( int nLayer );

	CUserCmd *m_pCmd;

	void HandleAnimationEvents( C_CSPlayer *pLocal, CCSGOPlayerAnimState *pState, C_AnimationLayer *layers, /*CUtlVector<uint16_t> uModifiers,*/ CUserCmd *cmd );

	std::array<C_AnimationLayer, 13> m_pPrevAnimOverlays;
	bool m_bHoldingSpace;

	AnimationInfo_t m_uServerAnimations;
	AnimationInfo_t m_uVisualAnimations;
	AnimationInfo_t m_uRenderAnimations;
	AnimationInfo_t m_uBodyAnimations;
	QAngle m_angChokedShotAngle;

	QAngle m_angPreviousAngle;

	void HandleServerAnimation( );
	void HandleAnimations( bool *bSendPacket, CUserCmd *cmd );
};

extern ServerAnimations g_ServerAnimations;