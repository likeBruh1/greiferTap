#include "SkinChanger.hpp"
#include "../../SDK/variables.hpp"
#include "../../SDK/Classes/weapon.hpp"
#include "../../SDK/Classes/player.hpp"
#include "../../SDK/Valve/CBaseHandle.hpp"
#include "KitParser.hpp"
#include "../../SDK/Classes/PropManager.hpp"
#include <algorithm>
#include <memory.h>
#include "../../SDK/Valve/recv_swap.hpp"
#include "../../Utils/extern/FnvHash.hpp"
#include "../../SDK/displacement.hpp"

static auto IsKnife( const int i ) -> bool {
	return ( i >= WEAPON_KNIFE_BAYONET && i < GLOVE_STUDDED_BLOODHOUND ) || i == WEAPON_KNIFE_T || i == WEAPON_KNIFE;
}

static CHandle< C_BaseCombatWeapon > hGloveHandle{ };
static uintptr_t hGloveRenderHandle{ };

SkinChanger g_SkinChanger;

void SkinChanger::Create( ) {
	RecvProp *prop = nullptr;
	Engine::g_PropManager.GetProp( XorStr( "DT_BaseViewModel" ), XorStr( "m_nSequence" ), &prop );
	m_sequence_hook = std::make_shared<RecvPropHook>( prop, &SequenceProxyFn );
}

void SkinChanger::Destroy( ) {
	m_sequence_hook->Unhook( );
	m_sequence_hook.reset( );
}

void SkinChanger::OnNetworkUpdate( bool start ) {
	auto &global = g_Vars.m_global_skin_changer;
	m_bSkipCheck = true;

	const auto pLocal = C_CSPlayer::GetLocalPlayer( );
	if( !g_pEngine->IsConnected( ) ) {
		m_bSkipCheck = false;
		return;
	}

	if( !m_bFirstTimeGloveHandle ) {
		hGloveHandle.Set( nullptr );
		m_bFirstTimeGloveHandle = true;
	}

	if( !start ) {
		if( global.m_update_skins && !global.m_update_gloves ) {
			float flDeltaTime = g_pGlobalVars->realtime - lastSkinUpdate;
			if( flDeltaTime >= 1.f ) {
				ForceItemUpdate( pLocal );
				global.m_update_skins = false;
				lastSkinUpdate = g_pGlobalVars->realtime;
			}
		}

		if( ( !global.m_active || !global.m_glove_changer ) || global.m_update_gloves ) {
			auto pGlove = hGloveHandle.Get( );
			if( pGlove && hGloveHandle.IsValid( ) ) {
				auto pNetworkable = pGlove->GetClientNetworkable( );
				if( pNetworkable ) {
					pNetworkable->SetDestroyedOnRecreateEntities( );
					pNetworkable->Release( );
				}

				hGloveHandle.Set( nullptr );
			}

			const auto glove_config = GetDataFromIndex( global.m_gloves_idx );
			if( ( ( global.m_update_gloves && g_pGlobalVars->realtime - lastGloveUpdate >= 0.5f ) || ( glove_config && !glove_config->m_enabled && glove_config->m_executed ) ) && !pLocal->IsDead( ) ) {
				g_Vars.globals.m_bForceFullUpdate = true;

				if( global.m_update_gloves )
					lastGloveUpdate = g_pGlobalVars->realtime;

				global.m_update_gloves = false;
				global.m_update_skins = false;

				if( glove_config )
					glove_config->m_executed = false;
			}
		}

		if( pLocal && global.m_glove_changer ) {
			GloveChanger( pLocal );
		}

		m_bSkipCheck = false;
		return;
	}

	if( !global.m_active ) {
		m_bSkipCheck = false;
		return;
	}

	PostDataUpdateStart( pLocal );

	m_bSkipCheck = false;
}

bool can_force_item_update( C_BaseCombatWeapon *item ) {
	for( auto &e : item->m_CustomMaterials( ) ) {
		if( e ) {
			const auto &is_valid = *( bool * )( ( uintptr_t )e + 4 + 20 + 4 ); // https://github.com/perilouswithadollarsign/cstrike15_src/blob/master/materialsystem/custom_material.h#L74
			if( !is_valid )
				return false;
		}
	}
	return true;
}

class CCStrike15ItemSystem;
class CCStrike15ItemSchema;

void SkinChanger::PostDataUpdateStart( C_CSPlayer *pLocal ) {
	if( !pLocal )
		return;

	if( pLocal->IsDead( ) )
		return;

	const auto local_index = pLocal->EntIndex( );

	player_info_t player_info;
	if( !g_pEngine->GetPlayerInfo( local_index, &player_info ) )
		return;

	auto &global = g_Vars.m_global_skin_changer;

	// Handle weapon configs
	{
		auto weapons = pLocal->m_hMyWeapons( );
		for( int i = 0; i < 48; ++i ) {
			auto weapon = ( C_BaseAttributableItem * )weapons[ i ].Get( );
			if( !weapon )
				continue;

			auto &definition_index = weapon->m_Item( ).m_iItemDefinitionIndex( );

			auto idx = IsKnife( definition_index ) ? global.m_knife_idx : definition_index;

			const auto active_conf = GetDataFromIndex( idx );
			if( IsKnife( definition_index ) )
				active_conf->m_enabled = g_Vars.m_global_skin_changer.m_knife_changer;

			if( active_conf ) {
				if( ( !active_conf->m_enabled || !global.m_active ) && active_conf->m_executed )
					global.m_update_skins = true;

				ApplyConfigOnAttributableItem( weapon, active_conf, player_info.xuid_low );
			}
			else {
				EraseOverrideIfExistsByIndex( definition_index );
			}
		}
	}

	const auto pViewModel = ( C_BaseViewModel * )pLocal->m_hViewModel( ).Get( );
	if( !pViewModel )
		return;

	const auto pViewModelWeapon = ( C_BaseAttributableItem * )pViewModel->m_hWeapon( ).Get( );
	if( !pViewModelWeapon )
		return;

	auto idx = pViewModelWeapon->m_Item( ).m_iItemDefinitionIndex( );
	if( g_KitParser.vecWeaponInfo.count( idx ) > 0 ) {
		const auto override_info = g_KitParser.vecWeaponInfo.at( idx );
		const auto override_model_index = g_pModelInfo->GetModelIndex( override_info.model );

		const auto weapon = reinterpret_cast< C_WeaponCSBaseGun * >( pViewModelWeapon );
		if( weapon ) {
			pViewModel->SetModelIndex( override_model_index );

			auto weapondata = weapon->GetCSWeaponData( );
			if( weapondata.IsValid( ) ) {
				const auto override_world_model_index = g_pModelInfo->GetModelIndex( weapondata->m_szWorldModel );
				const auto world_model = pViewModelWeapon->m_hWeaponWorldModel( ).Get( );
				if( world_model )
					world_model->SetModelIndex( override_world_model_index );
			}
		}
	}
}

void SkinChanger::DestroyGlove( ) {
	if( g_pEngine->IsConnected( ) || g_pEngine->IsInGame( ) )
		return;

	auto pGlove = hGloveHandle.Get( );
	if( pGlove && hGloveHandle.IsValid( ) ) {
		auto pNetworkable = pGlove->GetClientNetworkable( );
		if( pNetworkable ) {
			pNetworkable->SetDestroyedOnRecreateEntities( );
			pNetworkable->Release( );
		}

		hGloveHandle.Set( nullptr );
	}
}

void SkinChanger::EraseOverrideIfExistsByIndex( const int definition_index ) {
	if( g_KitParser.vecWeaponInfo.count( definition_index ) <= 0 )
		return;

	// We have info about the item not needed to be overridden
	const auto &original_item = g_KitParser.vecWeaponInfo.at( definition_index );
	auto &icon_override_map = m_icon_overrides;

	if( !original_item.icon )
		return;

	const auto override_entry = icon_override_map.find( original_item.icon );

	// We are overriding its icon when not needed
	if( override_entry != end( icon_override_map ) )
		icon_override_map.erase( override_entry ); // Remove the leftover override
}

#include "../../SDK/Valve/utlmap.hpp"

struct WeaponPaintableMaterial_t {
	char m_szName[ 128 ];
	char m_szOriginalMaterialName[ 128 ];
	char m_szFolderName[ 128 ];
	int m_nViewModelSize;						// texture size
	int m_nWorldModelSize;						// texture size
	float m_flWeaponLength;
	float m_flUVScale;
	bool m_bBaseTextureOverride;
	bool m_bMirrorPattern;
};

void ModifyPaintkitColor( C_BaseAttributableItem *pAttribute, CVariables::skin_changer_data *pConfig ) {
	int nKit = pConfig->m_filter_paint_kits ? pConfig->m_paint_kit : pConfig->m_paint_kit_no_filter;

	if( !pAttribute )
		return;

	auto &item = pAttribute->m_Item( );
	auto &definition_index = item.m_iItemDefinitionIndex( );

	static auto sig_address = Memory::Scan( XorStr( "client.dll" ), XorStr( "E8 ? ? ? ? FF 76 0C 8D 48 04 E8" ) );

	// Skip the opcode, read rel32 address
	static auto item_system_offset = *reinterpret_cast< std::int32_t * >( sig_address + 1 );

	// Add the offset to the end of the instruction
	static auto item_system_fn = reinterpret_cast< CCStrike15ItemSystem * ( * )( ) >( sig_address + 5 + item_system_offset );

	// Skip VTable, first member variable of ItemSystem is ItemSchema
	static auto item_schema = reinterpret_cast< CCStrike15ItemSchema * >( std::uintptr_t( item_system_fn( ) ) + sizeof( void * ) );

	// Skip the instructions between, skip the opcode, read rel32 address
	const auto get_paint_kit_definition_offset = *reinterpret_cast< std::int32_t * >( sig_address + 11 + 1 );

	// Add the offset to the end of the instruction
	const auto get_paint_kit_definition_fn = reinterpret_cast< CPaintKit * ( __thiscall * )( CCStrike15ItemSchema *, int ) >( sig_address + 11 + 5 + get_paint_kit_definition_offset );

	auto paintKit = get_paint_kit_definition_fn( item_schema, item.m_nFallbackPaintKit( ) );
	if( paintKit && pConfig->m_set_color && pConfig->m_change_paint_kit ) {
		// reset to default paint kit color (not work xd (sometime))
		if( pConfig->m_reset_color ) {
			pConfig->color_1.r = g_KitParser.vecPaintKits[ nKit ].defColor[ 0 ].r( ) / 255.f;
			pConfig->color_1.g = g_KitParser.vecPaintKits[ nKit ].defColor[ 0 ].g( ) / 255.f;
			pConfig->color_1.b = g_KitParser.vecPaintKits[ nKit ].defColor[ 0 ].b( ) / 255.f;
			pConfig->color_1.a = g_KitParser.vecPaintKits[ nKit ].defColor[ 0 ].a( ) / 255.f;

			pConfig->color_2.r = g_KitParser.vecPaintKits[ nKit ].defColor[ 1 ].r( ) / 255.f;
			pConfig->color_2.g = g_KitParser.vecPaintKits[ nKit ].defColor[ 1 ].g( ) / 255.f;
			pConfig->color_2.b = g_KitParser.vecPaintKits[ nKit ].defColor[ 1 ].b( ) / 255.f;
			pConfig->color_2.a = g_KitParser.vecPaintKits[ nKit ].defColor[ 1 ].a( ) / 255.f;

			pConfig->color_3.r = g_KitParser.vecPaintKits[ nKit ].defColor[ 2 ].r( ) / 255.f;
			pConfig->color_3.g = g_KitParser.vecPaintKits[ nKit ].defColor[ 2 ].g( ) / 255.f;
			pConfig->color_3.b = g_KitParser.vecPaintKits[ nKit ].defColor[ 2 ].b( ) / 255.f;
			pConfig->color_3.a = g_KitParser.vecPaintKits[ nKit ].defColor[ 2 ].a( ) / 255.f;

			pConfig->color_4.r = g_KitParser.vecPaintKits[ nKit ].defColor[ 3 ].r( ) / 255.f;
			pConfig->color_4.g = g_KitParser.vecPaintKits[ nKit ].defColor[ 3 ].g( ) / 255.f;
			pConfig->color_4.b = g_KitParser.vecPaintKits[ nKit ].defColor[ 3 ].b( ) / 255.f;
			pConfig->color_4.a = g_KitParser.vecPaintKits[ nKit ].defColor[ 3 ].a( ) / 255.f;

			// don't want to reset it again
			pConfig->m_reset_color = false;
		}

		// reset to default 
		if( !pConfig->m_custom_color && !pConfig->m_set_color ) {
			for( int n = 0; n <= 3; ++n ) {
				// no clue how this fails
				paintKit->rgbaColor[ n ] = g_KitParser.vecPaintKits[ nKit ].defColor[ n ];
			}
		}
		// modify our colors
		else {
			paintKit->rgbaColor[ 0 ] = ( pConfig->color_1.ToRegularColor( ) );
			paintKit->rgbaColor[ 1 ] = ( pConfig->color_2.ToRegularColor( ) );
			paintKit->rgbaColor[ 2 ] = ( pConfig->color_3.ToRegularColor( ) );
			paintKit->rgbaColor[ 3 ] = ( pConfig->color_4.ToRegularColor( ) );
		}

		// change paint kit phong
		if( pConfig->m_change_phong ) {
			paintKit->uchPhongExponent = ( unsigned char )std::clamp( pConfig->m_phong_exponent, 0.0f, 255.0f );
			paintKit->uchPhongAlbedoBoost = ( unsigned char )std::clamp( pConfig->m_phong_albedo_boost, 0.0f, 255.0f );
			paintKit->uchPhongIntensity = ( unsigned char )std::clamp( pConfig->m_phong_intensity, 0.0f, 255.0f );
		}

		static auto m_ItemOffset = Engine::g_PropManager.GetOffset( XorStr( "DT_BaseCombatWeapon" ), XorStr( "m_Item" ) );
		auto pItem = reinterpret_cast< void * >( uintptr_t( &item ) + m_ItemOffset );

		static auto CreateCustomWeaponMaterialsFn = reinterpret_cast< void( __thiscall * )( void *item, int nWeaponId, bool bIgnorePicMip, int diffuseTextureSize ) >(
			Memory::Scan( XorStr( "client.dll" ), XorStr( "55 8B EC 83 E4 ? 81 EC ? ? ? ? 53 56 57 8B F9 89 7C 24 ? E8" ) ) );

		static auto GetStaticDataFn = reinterpret_cast< void *( __thiscall * )( void *item ) >(
			Memory::Scan( XorStr( "client.dll" ), XorStr( "55 8B EC 51 56 57 8B F1 E8 ? ? ? ? 0F B7 8E" ) ) );

		// basically, where skins get updated/initialised it "compares" against already existing skins
		// to see if they already exist, if so then it uses the cached one - and if it's a new entry
		// or there's a difference, then it will add it to the skins list. 
		// https://github.com/perilouswithadollarsign/cstrike15_src/blob/f82112a2388b841d72cb62ca48ab1846dfcc11c8/game/shared/econ/econ_item_view.cpp#L2376-L2385
		// here is what it compares with, so the code below forces the game to think its a new entry
		// by modifying some bullshit number (m_flWeaponLength) by a little amount, and then the game
		// uses our new skin with this bullshit changed number

		auto pStaticData = GetStaticDataFn( reinterpret_cast< void * >( uintptr_t( pAttribute ) + Engine::Displacement.DT_BaseAttributableItem.m_Item ) );
		if( pStaticData ) {
			auto pPaintData = reinterpret_cast< CUtlVector< WeaponPaintableMaterial_t >* >( ( uintptr_t )( pStaticData )+0x19C );
			if( pPaintData ) {
				int nNumMaterialsToPaint = paintKit->bOnlyFirstMaterial ? 1 : pPaintData->Count( );

				// modify this paintkits bullshit number for every entry
				for( int nCustomMaterialIndex = 0; nCustomMaterialIndex < nNumMaterialsToPaint; nCustomMaterialIndex++ ) {
					( *pPaintData )[ nCustomMaterialIndex ].m_flWeaponLength += 0.00001f;
				}
			}
		}

		// call the function responsible for creating/updating paint kits
		//	CreateCustomWeaponMaterialsFn( reinterpret_cast< void * >( uintptr_t( pAttribute ) + Engine::Displacement.DT_BaseAttributableItem.m_Item ), definition_index, false, 9 );

		g_Vars.m_global_skin_changer.m_update_skins = true;

		// we set the color for this paintkit, dont do it again
		pConfig->m_set_color = false;
	}
}

void SkinChanger::ApplyConfigOnAttributableItem( C_BaseAttributableItem *pAttribute, CVariables::skin_changer_data *pConfig, const unsigned xuid_low ) {
	const auto pLocal = C_CSPlayer::GetLocalPlayer( );
	if( !pLocal )
		return;

	if( !pAttribute )
		return;

	if( !pConfig->m_enabled )
		return;

	int nKit = pConfig->m_filter_paint_kits ? pConfig->m_paint_kit : pConfig->m_paint_kit_no_filter;

	const int replaceKit = pConfig->m_filter_paint_kits ? nKit : g_KitParser.vecPaintKits[ nKit ].id;

	auto &item = pAttribute->m_Item( );
	auto &global = g_Vars.m_global_skin_changer;

	auto &definition_index = item.m_iItemDefinitionIndex( );

	// Force fallback values to be used.
	item.m_iItemIDHigh( ) = -1;

	// Set the owner of the weapon to our lower XUID. (fixes StatTrak)
	item.m_iAccountID( ) = xuid_low;

	item.m_nFallbackPaintKit( ) = replaceKit;

	//if( !g_Vars.globals.dotest )
	item.m_nFallbackSeed( ) = int( pConfig->m_seed );

	item.m_iEntityQuality( ) = 0;

	if( pConfig->m_stat_trak ) {
		item.m_nFallbackStatTrak( ) = 1337;
		item.m_iEntityQuality( ) = 9;
	}
	else {
		item.m_nFallbackStatTrak( ) = -1;
		item.m_iEntityQuality( ) = 0;
	}

	ModifyPaintkitColor( pAttribute, pConfig );

	item.m_flFallbackWear( ) = pConfig->m_wear > 99.f ? 0.00001f : ( pConfig->m_wear != 0.f ) ? ( 100.f - pConfig->m_wear ) / 100.f : pConfig->m_wear;

	auto &icon_override_map = m_icon_overrides;

	bool knife = IsKnife( definition_index );

	int definition_override = 0;

	if( knife ) {
		if( global.m_knife_changer ) {
			definition_override = global.m_knife_idx;
			item.m_iEntityQuality( ) = 3;
		}
		else {
			// definition_override = ( pLocal->m_iTeamNum( ) == TEAM_CT ) ? WEAPON_KNIFE : WEAPON_KNIFE_T;
		}
	}
	else if( ( pConfig->m_definition_index >= GLOVE_STUDDED_BLOODHOUND && pConfig->m_definition_index <= GLOVE_HYDRA ) || pConfig->m_definition_index == 4725 /*BROKEN FANG GLOVES*/ )
		definition_override = global.m_gloves_idx;

	if( definition_override && ( definition_override != definition_index ) ) {
		// We have info about what we gonna override it to
		if( g_KitParser.vecWeaponInfo.count( definition_override ) > 0 ) {
			const auto replacement_item = &g_KitParser.vecWeaponInfo.at( definition_override );

			const auto old_definition_index = definition_index;

			item.m_iItemDefinitionIndex( ) = definition_override;

			// Set the weapon model index -- required for paint kits to work on replacement items after the 29/11/2016 update.
			auto idx = g_pModelInfo->GetModelIndex( replacement_item->model );
			pAttribute->SetModelIndex( idx );

			auto networkable = pAttribute->GetClientNetworkable( );
			if( networkable ) {
				networkable->PreDataUpdate( 0 );
			}

			// We didn't override 0, but some actual weapon, that we have data for
			if( old_definition_index ) {
				if( g_KitParser.vecWeaponInfo.count( old_definition_index ) > 0 ) {
					const auto original_item = &g_KitParser.vecWeaponInfo.at( old_definition_index );
					if( original_item->icon && replacement_item->icon )
						icon_override_map[ original_item->icon ] = replacement_item->icon;
				}
			}
		}
	}
	else {
		EraseOverrideIfExistsByIndex( definition_index );
	}

	pConfig->m_executed = false;
}

void SkinChanger::GloveChanger( C_CSPlayer *pLocal ) {
	if( !pLocal )
		return;

	const auto iLocalIndex = pLocal->EntIndex( );

	player_info_t player_info;
	if( !g_pEngine->GetPlayerInfo( iLocalIndex, &player_info ) )
		return;

	auto &global = g_Vars.m_global_skin_changer;
	const auto glove_config = GetDataFromIndex( global.m_gloves_idx );

	const auto hWearables = pLocal->m_hMyWearables( );

	if( !glove_config || global.m_gloves_idx == 0 || !hWearables ) {
		return;
	}

	if( hWearables ) {
		auto pWearable = ( C_BaseAttributableItem * )hWearables[ 0 ].Get( );
		if( !pWearable ) {
			// Try to get our last created glove
			const auto pTheGlove = ( C_BaseAttributableItem * )hGloveHandle.Get( );
			if( pTheGlove ) // Our glove still exists
			{
				hWearables[ 0 ] = hGloveHandle;
				pWearable = pTheGlove;
			}
		}

		if( !pWearable ) {
			int iEntry = g_pEntityList->GetHighestEntityIndex( ) + 1;

			for( int i = 0; i < g_pEntityList->GetHighestEntityIndex( ); i++ ) {
				auto pEntity = g_pEntityList->GetClientEntity( i );

				if( pEntity ) {
					if( pEntity->GetClientClass( ) ) {
						if( pEntity->GetClientClass( )->m_ClassID == CPlasma ) {
							iEntry = i;
							break;
						}
					}
				}
			}

			for( ClientClass *pClass = g_pClient->GetAllClasses( ); pClass; pClass = pClass->m_pNext ) {
				if( pClass->m_ClassID != CEconWearable )
					continue;

				int	iSerial = 4095;

				reinterpret_cast< CreateClientClassFn >( pClass->m_pCreateFn )( iEntry, iSerial );
				hWearables[ 0 ] = iEntry | iSerial << 16;

				break;
			}

			pWearable = ( C_BaseAttributableItem * )g_pEntityList->GetClientEntity( iEntry );
			if( pWearable ) {
				static auto fnEquip
					= reinterpret_cast< int( __thiscall * )( void *, void * ) >(
						Memory::Scan( XorStr( "client.dll" ), XorStr( "55 8B EC 83 EC 10 53 8B 5D 08 57 8B F9" ) )
						);

				static auto fnInitializeAttributes
					= reinterpret_cast< int( __thiscall * )( void * ) >(
						Memory::Scan( XorStr( "client.dll" ), XorStr( "55 8B EC 83 E4 F8 83 EC 0C 53 56 8B F1 8B 86" ) )
						);

				//glove_config->m_executed = true;

				auto config = glove_config;

				const auto replacement_item = &g_KitParser.vecWeaponInfo.at( ( int )glove_config->m_definition_index );

				pWearable->m_Item( ).m_iItemIDHigh( ) = -1;
				pWearable->m_Item( ).m_iItemDefinitionIndex( ) = ( int )glove_config->m_definition_index;
				pWearable->m_Item( ).m_nFallbackPaintKit( ) = config->m_filter_paint_kits ? config->m_paint_kit : g_KitParser.vecPaintKits[ config->m_paint_kit_no_filter ].id;
				pWearable->m_Item( ).m_iEntityQuality( ) = 4;
				pWearable->m_Item( ).m_iAccountID( ) = player_info.xuid_low;;
				pWearable->m_Item( ).m_bInitialized( ) = true;
				pWearable->m_Item( ).m_nFallbackSeed( ) = glove_config->m_seed;

				pWearable->SetModelIndex( g_pModelInfo->GetModelIndex( replacement_item->world_model ) );

				fnEquip( pWearable, pLocal );
				*( int * )( ( uintptr_t )pLocal + 0xA20 ) = 1; // m_nBody
				fnInitializeAttributes( pWearable );

				//printf( "pre: %i\n", pWearable->GetClientRenderable( )->RenderHandle( ) );
				g_pClientLeafSystem->_CreateRenderableHandle( pWearable );

				if( pWearable->GetClientRenderable( ) )
					hGloveRenderHandle = pWearable->GetClientRenderable( )->RenderHandle( );

				auto networkable = pWearable->GetClientNetworkable( );
				if( networkable ) {
					networkable->PreDataUpdate( 0 );
				}
			}
		}
		else {

			if( pWearable->GetClientRenderable( ) ) {
				if( pWearable->GetClientRenderable( )->RenderHandle( ) == 0xffff ||
					pWearable->GetClientRenderable( )->RenderHandle( ) != hGloveRenderHandle ) {

					g_pClientLeafSystem->RemoveRenderable( hGloveRenderHandle );
					g_pClientLeafSystem->_CreateRenderableHandle( pWearable );
					hGloveRenderHandle = pWearable->GetClientRenderable( )->RenderHandle( );
				}
			}

		}
	}
}

void SkinChanger::SequenceProxyFn( const CRecvProxyData *proxy_data_const, void *entity, void *output ) {
	if( g_SkinChanger.m_sequence_hook ) {
		static auto original_fn = g_SkinChanger.m_sequence_hook->GetOriginalFunction( );

		// Remove the constness from the proxy data allowing us to make changes.
		const auto proxy_data = const_cast< CRecvProxyData * >( proxy_data_const );

		const auto view_model = static_cast< C_BaseViewModel * >( entity );

		g_SkinChanger.DoSequenceRemapping( proxy_data, view_model );

		// Call the original function with our edited data.
		original_fn( proxy_data_const, entity, output );
	}
}

void SkinChanger::DoSequenceRemapping( CRecvProxyData *data, C_BaseViewModel *entity ) {
	const auto pLocal = C_CSPlayer::GetLocalPlayer( );
	if( !pLocal || pLocal->IsDead( ) )
		return;

	const auto pOwner = entity->m_hOwner( ).Get( );
	if( pOwner != pLocal )
		return;

	const auto pViewModelWeapon = entity->m_hWeapon( ).Get( );
	if( !pViewModelWeapon )
		return;

	auto idx = pViewModelWeapon->m_Item( ).m_iItemDefinitionIndex( );
	if( g_KitParser.vecWeaponInfo.count( idx ) <= 0 )
		return;

	if( !g_Vars.m_global_skin_changer.m_knife_changer )
		return;

	const auto weapon_info = &g_KitParser.vecWeaponInfo.at( idx );

	if( weapon_info ) {
		const auto override_model = weapon_info->model;

		auto &sequence = data->m_Value.m_Int;
		sequence = GetNewAnimation( hash_32_fnv1a_const( override_model ), sequence, entity );
	}
}

int SkinChanger::GetNewAnimation( const uint32_t model, const int sequence, C_BaseViewModel *viewModel ) {

	// This only fixes if the original knife was a default knife.
	// The best would be having a function that converts original knife's sequence
	// into some generic enum, then another function that generates a sequence
	// from the sequences of the new knife. I won't write that.
	enum ESequence {
		SEQUENCE_DEFAULT_DRAW = 0,
		SEQUENCE_DEFAULT_IDLE1 = 1,
		SEQUENCE_DEFAULT_IDLE2 = 2,
		SEQUENCE_DEFAULT_LIGHT_MISS1 = 3,
		SEQUENCE_DEFAULT_LIGHT_MISS2 = 4,
		SEQUENCE_DEFAULT_HEAVY_MISS1 = 9,
		SEQUENCE_DEFAULT_HEAVY_HIT1 = 10,
		SEQUENCE_DEFAULT_HEAVY_BACKSTAB = 11,
		SEQUENCE_DEFAULT_LOOKAT01 = 12,

		SEQUENCE_BUTTERFLY_DRAW = 0,
		SEQUENCE_BUTTERFLY_DRAW2 = 1,
		SEQUENCE_BUTTERFLY_LOOKAT01 = 13,
		SEQUENCE_BUTTERFLY_LOOKAT03 = 15,

		SEQUENCE_FALCHION_IDLE1 = 1,
		SEQUENCE_FALCHION_HEAVY_MISS1 = 8,
		SEQUENCE_FALCHION_HEAVY_MISS1_NOFLIP = 9,
		SEQUENCE_FALCHION_LOOKAT01 = 12,
		SEQUENCE_FALCHION_LOOKAT02 = 13,

		SEQUENCE_DAGGERS_IDLE1 = 1,
		SEQUENCE_DAGGERS_LIGHT_MISS1 = 2,
		SEQUENCE_DAGGERS_LIGHT_MISS5 = 6,
		SEQUENCE_DAGGERS_HEAVY_MISS2 = 11,
		SEQUENCE_DAGGERS_HEAVY_MISS1 = 12,

		SEQUENCE_BOWIE_IDLE1 = 1,
	};

	auto random_sequence = [ ] ( const int low, const int high ) -> int {
		return rand( ) % ( high - low + 1 ) + low;
	};

	// Hashes for best performance.
	switch( model ) {
		case hash_32_fnv1a_const( ( "models/weapons/v_knife_butterfly.mdl" ) ):
		{
			switch( sequence ) {
				case SEQUENCE_DEFAULT_DRAW:
					return random_sequence( SEQUENCE_BUTTERFLY_DRAW, SEQUENCE_BUTTERFLY_DRAW2 );
				case SEQUENCE_DEFAULT_LOOKAT01:
					return random_sequence( SEQUENCE_BUTTERFLY_LOOKAT01, SEQUENCE_BUTTERFLY_LOOKAT03 );
				default:
					return sequence + 1;
			}
		}
		case hash_32_fnv1a_const( ( "models/weapons/v_knife_falchion_advanced.mdl" ) ):
		{
			switch( sequence ) {
				case SEQUENCE_DEFAULT_IDLE2:
					return SEQUENCE_FALCHION_IDLE1;
				case SEQUENCE_DEFAULT_HEAVY_MISS1:
					return random_sequence( SEQUENCE_FALCHION_HEAVY_MISS1, SEQUENCE_FALCHION_HEAVY_MISS1_NOFLIP );
				case SEQUENCE_DEFAULT_LOOKAT01:
					return random_sequence( SEQUENCE_FALCHION_LOOKAT01, SEQUENCE_FALCHION_LOOKAT02 );
				case SEQUENCE_DEFAULT_DRAW:
				case SEQUENCE_DEFAULT_IDLE1:
					return sequence;
				default:
					return sequence - 1;
			}
		}
		case hash_32_fnv1a_const( ( "models/weapons/v_knife_push.mdl" ) ):
		{
			switch( sequence ) {
				case SEQUENCE_DEFAULT_IDLE2:
					return SEQUENCE_DAGGERS_IDLE1;
				case SEQUENCE_DEFAULT_LIGHT_MISS1:
				case SEQUENCE_DEFAULT_LIGHT_MISS2:
					return random_sequence( SEQUENCE_DAGGERS_LIGHT_MISS1, SEQUENCE_DAGGERS_LIGHT_MISS5 );
				case SEQUENCE_DEFAULT_HEAVY_MISS1:
					return random_sequence( SEQUENCE_DAGGERS_HEAVY_MISS2, SEQUENCE_DAGGERS_HEAVY_MISS1 );
				case SEQUENCE_DEFAULT_HEAVY_HIT1:
				case SEQUENCE_DEFAULT_HEAVY_BACKSTAB:
				case SEQUENCE_DEFAULT_LOOKAT01:
					return sequence + 3;
				case SEQUENCE_DEFAULT_DRAW:
				case SEQUENCE_DEFAULT_IDLE1:
					return sequence;
				default:
					return sequence + 2;
			}
		}
		case hash_32_fnv1a_const( ( "models/weapons/v_knife_survival_bowie.mdl" ) ):
		{
			switch( sequence ) {
				case SEQUENCE_DEFAULT_DRAW:
				case SEQUENCE_DEFAULT_IDLE1:
					return sequence;
				case SEQUENCE_DEFAULT_IDLE2:
					return SEQUENCE_BOWIE_IDLE1;
				default:
					return sequence - 1;
			}
		}
		case hash_32_fnv1a_const( ( "models/weapons/v_knife_ursus.mdl" ) ):
		case hash_32_fnv1a_const( ( "models/weapons/v_knife_skeleton.mdl" ) ):
		case hash_32_fnv1a_const( ( "models/weapons/v_knife_outdoor.mdl" ) ):
		case hash_32_fnv1a_const( ( "models/weapons/v_knife_canis.mdl" ) ):
		case hash_32_fnv1a_const( ( "models/weapons/v_knife_cord.mdl" ) ):
		{
			switch( sequence ) {
				case SEQUENCE_DEFAULT_DRAW:
					return random_sequence( SEQUENCE_BUTTERFLY_DRAW, SEQUENCE_BUTTERFLY_DRAW2 );
				case SEQUENCE_DEFAULT_LOOKAT01:
					return random_sequence( SEQUENCE_BUTTERFLY_LOOKAT01, 14 );
				default:
					return sequence + 1;
			}
		}
		case hash_32_fnv1a_const( ( "models/weapons/v_knife_stiletto.mdl" ) ):
		{
			switch( sequence ) {
				case SEQUENCE_DEFAULT_LOOKAT01:
					return random_sequence( 12, 13 );
			}
		}
		case hash_32_fnv1a_const( ( "models/weapons/v_knife_widowmaker.mdl" ) ):
		{
			switch( sequence ) {
				case SEQUENCE_DEFAULT_LOOKAT01:
					return random_sequence( 14, 15 );
			}
		}

		default:
			return sequence;
	}

	return sequence;
}

CVariables::skin_changer_data *SkinChanger::GetDataFromIndex( int idx ) {
	auto &skin_data = g_Vars.m_skin_changer;
	for( size_t i = 0u; i < skin_data.Size( ); ++i ) {
		auto skin = skin_data[ i ];
		if( skin->m_definition_index == idx )
			return skin;
	}
	return nullptr;
}

void SkinChanger::ForceItemUpdate( C_CSPlayer *pLocal ) {
	if( !pLocal || pLocal->IsDead( ) )
		return;

	auto ForceUpdate = [ ] ( C_BaseCombatWeapon *pItem ) {
		C_EconItemView *view = &pItem->m_Item( );

		if( !view )
			return;

		if( !pItem->GetClientNetworkable( ) )
			return;

		auto clearRefCountedVector = [ ] ( CUtlVector< IRefCounted * > &vec ) {
			for( int i = 0; i < vec.m_Size; ++i ) {
				if( &vec.m_Memory ) {
					if( vec.m_Memory.m_pMemory ) {
						auto &element = vec.m_Memory.m_pMemory[ i ];
						if( element ) {
							element->unreference( );
							element = nullptr;
						}
					}
				}
			}
			vec.m_Size = 0;
		};

		auto clearCustomMaterials = [ ] ( CUtlVector< IRefCounted * > &vec ) {
			for( int i = 0; i < vec.m_Size; ++i ) {
				if( &vec.m_Memory ) {
					if( vec.m_Memory.m_pMemory ) {
						auto &element = vec.m_Memory.m_pMemory[ i ];
						if( element ) {
							// actually makes no sense
							*( int * )( ( ( uintptr_t )element ) + 0x10 ) = 0;
							*( int * )( ( ( uintptr_t )element ) + 0x18 ) = 0;
							*( int * )( ( ( uintptr_t )element ) + 0x20 ) = 0;
							*( int * )( ( ( uintptr_t )element ) + 0x24 ) = 0;
						}

						vec.m_Memory.m_pMemory[ i ] = nullptr;
					}
				}
			}

			vec.m_Size = 0;
		};

		pItem->m_bCustomMaterialInitialized( ) = false;
		clearCustomMaterials( pItem->m_CustomMaterials( ) );
		clearCustomMaterials( view->m_CustomMaterials( ) );
		clearRefCountedVector( view->m_VisualsDataProcessors( ) );

		const auto pNetworkable = pItem->GetClientNetworkable( );
		if( pNetworkable ) {
			pNetworkable->PostDataUpdate( 0 );
			pNetworkable->OnDataChanged( 0 );
		}

		void *SFWeaponSelection = FindHudElement<void *>( XorStr( "SFWeaponSelection" ) );

		using ShowAndUpdateSelection_t = void( __thiscall * )( void *, int, C_BaseCombatWeapon *, bool );
		static auto ShowAndUpdateSelection = reinterpret_cast< ShowAndUpdateSelection_t >( Memory::CallableFromRelative( Memory::Scan( XorStr( "client.dll" ), XorStr( "E8 ? ? ? ? A1 ? ? ? ? F3 0F 10 40 ? C6 83" ), false ) ) );
		if( ShowAndUpdateSelection && SFWeaponSelection )
			ShowAndUpdateSelection( SFWeaponSelection, 0, pItem, false );
	};
	auto &global = g_Vars.m_global_skin_changer;
	auto weapons = pLocal->m_hMyWeapons( );
	for( size_t i = 0; i < 48; ++i ) {
		auto weaponHandle = weapons[ i ];
		if( !weaponHandle.IsValid( ) )
			break;

		auto pWeapon = ( C_WeaponCSBaseGun * )weaponHandle.Get( );
		if( !pWeapon )
			continue;

		//if( can_force_item_update( ( C_BaseCombatWeapon * )pWeapon ) )
		ForceUpdate( pWeapon );
	}

	UpdateHud( );
}

void SkinChanger::UpdateHud( ) {

}