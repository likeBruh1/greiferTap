#pragma once
#include "../../SDK/sdk.hpp"
#include "../Rage/Animations.hpp"

class SkinChanger {
public:
	void Create( );
	void Destroy( );
	void OnNetworkUpdate( bool start = true );
	void PostDataUpdateStart( C_CSPlayer *local );

	void DestroyGlove( );
	bool m_bSkipCheck;
	std::unordered_map< std::string_view, std::string_view > m_icon_overrides;
private:
	void EraseOverrideIfExistsByIndex( const int definition_index );
	void ApplyConfigOnAttributableItem( C_BaseAttributableItem *item, CVariables::skin_changer_data *config, const unsigned xuid_low );
	void GloveChanger( C_CSPlayer *local );
	static void SequenceProxyFn( const CRecvProxyData *proxy_data_const, void *entity, void *output );
	void DoSequenceRemapping( CRecvProxyData *data, C_BaseViewModel *entity );
	int GetNewAnimation( const uint32_t model, const int sequence, C_BaseViewModel *viewModel );
	CVariables::skin_changer_data *GetDataFromIndex( int idx );
	void ForceItemUpdate( C_CSPlayer *local );
	void UpdateHud( );

	RecvPropHook::Shared m_sequence_hook = nullptr;

	float lastSkinUpdate = 0.0f;
	float lastGloveUpdate = 0.0f;
	bool m_bFirstTimeGloveHandle = false;
};

extern SkinChanger g_SkinChanger;