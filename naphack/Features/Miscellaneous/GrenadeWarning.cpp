#include "GrenadeWarning.hpp"
#include "../Rage/Autowall.hpp"
#include "../Visuals/Visuals.hpp"
#include "../Rage/TickbaseShift.hpp"

#include "../../SDK/Classes/weapon.hpp"

GrenadeWarning g_GrenadeWarning;

bool GrenadeWarning::GrenadeData_t::PhysicsSimulate( ) {
	if( m_bDetonated )
		return true;

	const auto flVelocityZ = m_vecVelocity.z - ( g_Vars.sv_gravity->GetFloat( ) * 0.4f ) * g_pGlobalVars->interval_per_tick;

	const auto move = Vector(
		m_vecVelocity.x * g_pGlobalVars->interval_per_tick,
		m_vecVelocity.y * g_pGlobalVars->interval_per_tick,
		( m_vecVelocity.z + flVelocityZ ) / 2.f * g_pGlobalVars->interval_per_tick
	);

	m_vecVelocity.z = flVelocityZ;

	auto trace = CGameTrace( );

	PhysicsPushEntity( move, trace );

	if( m_bDetonated )
		return true;

	if( trace.fraction != 1.f ) {
		UpdatePath( true );

		PerformFlyCollision( trace );
	}

	return false;
}

void GrenadeWarning::GrenadeData_t::PhysicsTraceEntity( const Vector &src, const Vector &dst, std::uint32_t mask, CGameTrace &trace ) {
	TraceHull(
		src, dst, { -2.f, -2.f, -2.f }, { 2.f, 2.f, 2.f },
		mask, m_pOwner, m_iCollisionGroup, &trace
	);

	if( trace.startsolid && ( trace.contents & CONTENTS_CURRENT_90 ) ) {
		TraceHull(
			src, dst, { -2.f, -2.f, -2.f }, { 2.f, 2.f, 2.f },
			mask & ~CONTENTS_CURRENT_90, m_pOwner, m_iCollisionGroup, &trace
		);
	}

	if( !trace.DidHit( )
		|| !trace.hit_entity
		|| !reinterpret_cast< C_CSPlayer * >( trace.hit_entity )->IsPlayer( ) )
		return;

	TraceLine( src, dst, mask, m_pOwner, m_iCollisionGroup, &trace );
}

void GrenadeWarning::GrenadeData_t::PhysicsPushEntity( const Vector &push, CGameTrace &trace ) {
	PhysicsTraceEntity( m_vecOrigin, m_vecOrigin + push,
						m_iCollisionGroup == COLLISION_GROUP_DEBRIS
						? ( MASK_SOLID | CONTENTS_CURRENT_90 ) & ~CONTENTS_MONSTER
						: MASK_SOLID | CONTENTS_OPAQUE | CONTENTS_IGNORE_NODRAW_OPAQUE | CONTENTS_CURRENT_90 | CONTENTS_HITBOX,
						trace
	);

	if( trace.startsolid ) {
		m_iCollisionGroup = COLLISION_GROUP_INTERACTIVE_DEBRIS;

		TraceLine(
			m_vecOrigin - push, m_vecOrigin + push,
			( MASK_SOLID | CONTENTS_CURRENT_90 ) & ~CONTENTS_MONSTER,
			m_pOwner, m_iCollisionGroup, &trace
		);
	}

	if( trace.fraction ) {
		m_vecOrigin = trace.endpos;
	}

	if( !trace.hit_entity )
		return;

	if( reinterpret_cast< C_CSPlayer * >( trace.hit_entity )->IsPlayer( )
		|| m_iWeapIndex != WEAPON_TAGRENADE && m_iWeapIndex != WEAPON_MOLOTOV && m_iWeapIndex != WEAPON_INC )
		return;

	if( m_iWeapIndex != WEAPON_TAGRENADE
		&& trace.plane.normal.z < std::cos( DEG2RAD( g_Vars.weapon_molotov_maxdetonateslope->GetFloat( ) ) ) )
		return;

	Detonate( true );
}

void GrenadeWarning::GrenadeData_t::PerformFlyCollision( CGameTrace &trace ) {
	auto flSurfaceElasticity = 1.f;

	if( trace.hit_entity ) {
		if( Autowall::IsBreakable( reinterpret_cast< C_CSPlayer * >( trace.hit_entity ) ) ) {

			if( reinterpret_cast< C_CSPlayer * >( trace.hit_entity ) && reinterpret_cast< C_CSPlayer * >( trace.hit_entity )->m_iHealth( ) <= 0 ) {
				m_vecVelocity *= 0.4f;
				m_pOwner = reinterpret_cast< C_CSPlayer * >( trace.hit_entity );
				return;
			}
		}

		const auto bIsPlayer = reinterpret_cast< C_CSPlayer * >( trace.hit_entity )->IsPlayer( );
		if( bIsPlayer ) {
			flSurfaceElasticity = 0.3f;
		}

		if( trace.hit_entity->EntIndex( ) ) {
			if( bIsPlayer
				&& m_pLastHitEntity == trace.hit_entity ) {
				m_iCollisionGroup = COLLISION_GROUP_DEBRIS;

				return;
			}

			m_pLastHitEntity = trace.hit_entity;
		}
	}

	auto vecVelocity = Vector( );

	const auto flBack = m_vecVelocity.Dot( trace.plane.normal ) * 2.f;

	for( auto i = 0u; i < 3u; i++ ) {
		const auto change = trace.plane.normal[ i ] * flBack;

		vecVelocity[ i ] = m_vecVelocity[ i ] - change;

		if( std::fabs( vecVelocity[ i ] ) >= 1.f )
			continue;

		vecVelocity[ i ] = 0.f;
	}

	vecVelocity *= std::clamp< float >( flSurfaceElasticity * 0.45f, 0.f, 0.9f );

	if( trace.plane.normal.z > 0.7f ) {
		const auto flSpeed = vecVelocity.LengthSquared( );
		if( flSpeed > 96000.f ) {
			const auto l = vecVelocity.Normalized( ).Dot( trace.plane.normal );
			if( l > 0.5f ) {
				vecVelocity *= 1.f - l + 0.5f;
			}
		}

		if( flSpeed < 400.f ) {
			m_vecVelocity = Vector( 0, 0, 0 );
		}
		else {
			m_vecVelocity = vecVelocity;

			PhysicsPushEntity( vecVelocity * ( ( 1.f - trace.fraction ) * g_pGlobalVars->interval_per_tick ), trace );
		}
	}
	else {
		m_vecVelocity = vecVelocity;

		PhysicsPushEntity( vecVelocity * ( ( 1.f - trace.fraction ) * g_pGlobalVars->interval_per_tick ), trace );
	}

	if( m_nBounces > 20 )
		return Detonate( false );

	++m_nBounces;
}

void GrenadeWarning::GrenadeData_t::Think( ) {
	switch( m_iWeapIndex ) {
		case WEAPON_SMOKEGRENADE:
			if( m_vecVelocity.LengthSquared( ) <= 0.01f ) {
				Detonate( false );
			}

			break;
		case WEAPON_DECOY:
			if( m_vecVelocity.LengthSquared( ) <= 0.04f ) {
				Detonate( false );
			}

			break;
		case WEAPON_FLASHBANG:
		case WEAPON_HEGRENADE:
		case WEAPON_MOLOTOV:
		case WEAPON_INC:
			if( TICKS_TO_TIME( m_iTick ) > m_flDetonationTime ) {
				Detonate( false );
			}

			break;
	}

	m_iNextThinkTick = m_iTick + TIME_TO_TICKS( 0.2f );
}

void GrenadeWarning::GrenadeData_t::Predict( const Vector &origin, const Vector &velocity, float throw_time, int offset ) {
	m_vecOrigin = origin;
	m_vecVelocity = velocity;
	m_iCollisionGroup = COLLISION_GROUP_PROJECTILE;

	const auto iTick = TIME_TO_TICKS( 1.f / 30.f );

	m_iLastUpdateTick = -iTick;

	switch( m_iWeapIndex ) {
		case WEAPON_SMOKEGRENADE: m_iNextThinkTick = TIME_TO_TICKS( 1.5f ); break;
		case WEAPON_DECOY: m_iNextThinkTick = TIME_TO_TICKS( 2.f ); break;
		case WEAPON_FLASHBANG:
		case WEAPON_HEGRENADE:
			m_flDetonationTime = 1.5f;
			m_iNextThinkTick = TIME_TO_TICKS( 0.02f );

			break;
		case WEAPON_MOLOTOV:
		case WEAPON_INC:
			m_flDetonationTime = g_Vars.molotov_throw_detonate_time->GetFloat( );
			m_iNextThinkTick = TIME_TO_TICKS( 0.02f );

			break;
	}

	for( ; m_iTick < TIME_TO_TICKS( 60.f ); ++m_iTick ) {
		if( m_iNextThinkTick <= m_iTick ) {
			Think( );
		}

		if( m_iTick < offset )
			continue;

		if( PhysicsSimulate( ) )
			break;

		if( m_iLastUpdateTick + iTick > m_iTick )
			continue;

		UpdatePath( false );
	}

	if( m_iLastUpdateTick + iTick <= m_iTick ) {
		UpdatePath( false );
	}

	m_flExpireTime = throw_time + TICKS_TO_TIME( m_iTick );
}

bool GrenadeWarning::GrenadeData_t::Draw( ) const {
	if( m_Path.size( ) <= 1u
		|| g_pGlobalVars->curtime >= m_flExpireTime )
		return false;

	auto pLocal = C_CSPlayer::GetLocalPlayer( );
	if( !pLocal )
		return false;

	float flDistance = pLocal->m_vecOrigin( ).Distance( m_vecOrigin ) / 12;

	if( flDistance > 200.f )
		return false;

	const auto vecScreenSize = Render::GetScreenSize( );
	const float iScreenWidth = vecScreenSize.x;
	const float iScreenHeight = vecScreenSize.y;

	Vector2D vPrevScreen2D = { };
	Vector vFinalPoint = std::get< Vector >( m_Path.front( ) );
	auto bPrevScreen = Render::Engine::WorldToScreen( std::get< Vector >( m_Path.front( ) ), vPrevScreen2D );

	for( auto i = 1u; i < m_Path.size( ); ++i ) {
		auto path = m_Path[ i ];

		Vector2D vCurScreen = { };
		const auto bCurScreen = Render::Engine::WorldToScreen( std::get< Vector >( m_Path.at( i ) ), vCurScreen );

		if( bPrevScreen && bCurScreen ) {
			if( g_Vars.esp.grenade_proximity_warning || g_Vars.esp.grenade_prediction ) {
				auto color = m_bLocalPredicted ? g_Vars.esp.grenade_prediction_color.ToRegularColor( ) : g_Vars.esp.grenade_proximity_warning_color.ToRegularColor( );

				if( m_bLocalPredicted || ( !m_bLocalPredicted && !m_pNadeEntity->IsDormant( ) ) ) {

					// temporarily until i figure out perfect frequency duration thingy so it doesnt
					// flicker and doesnt jitter when moving (render multiple times)
					if( m_bLocalPredicted /*|| true */ ) {
						Render::Engine::Line( vPrevScreen2D, vCurScreen, color );
						// s/o esoterik polak maddie d3x dex boris templar wzn nave ko1n imitator raxer zbe etc etc
						// badster vice machete nitsuj wav kamay kolo arctosa searchy ingame1128 ducarii etc etc etc
						// Render::Engine::Line( vPrevScreen2D + Vector2D( 1, 0 ), vCurScreen + Vector2D( 1, 0 ), color );
						// Render::Engine::Line( vPrevScreen2D - Vector2D( 1, 0 ), vCurScreen - Vector2D( 1, 0 ), color );
					}
					else {
						/*g_pGlowObjectManager->AddGlowBox( std::get< Vector >( m_Path.at( i ) ), vFinalPoint,
														  0.5f, color, g_pGlobalVars->interval_per_tick );*/
						Render::Engine::Line( vPrevScreen2D, vCurScreen, color );
					}

					if( m_bLocalPredicted ) {
						const bool bLastPoint = i == m_Path.size( ) - 1;
						if( path.second || bLastPoint ) {
							Vector2D vBounceScreen = { };
							if( Render::Engine::WorldToScreen( bLastPoint ? std::get< Vector >( m_Path.at( i ) ) : path.first, vBounceScreen ) ) {
								// Render::Engine::CircleFilled( vBounceScreen.x + 1, vBounceScreen.y + 1, 2, 4, bLastPoint ? Color( 255, 0, 0, 180 ) : Color( 255, 255, 255, 180 ) );
								Render::Engine::Rect( vBounceScreen - 2, Vector2D{ 4, 4 }, bLastPoint ? Color( 255, 0, 0, color.a( ) ) : Color( 255, 255, 255, color.a( ) ) );
							}
						}
					}
				}
			}
		}

		vFinalPoint = std::get< Vector >( m_Path.at( i ) );
		vPrevScreen2D = vCurScreen;
		bPrevScreen = bCurScreen;
	}

	bool bReached = false;
	CGameTrace fireTrace;
	CTraceFilter fireFilter;
	fireFilter.pSkip = m_pNadeEntity;
	if( m_iWeapIndex == WEAPON_MOLOTOV || m_iWeapIndex == WEAPON_INC ) {
		Autowall::TraceLine( vFinalPoint + Vector( 0.f, 0.f, 10.f ), vFinalPoint - Vector( 0.f, 0.f, 119.8f ), MASK_SOLID, &fireFilter, &fireTrace );

		if( fireTrace.fraction != 1.f ) {
			bReached = true;

			vFinalPoint = fireTrace.endpos;
		}
	}

	if( m_bLocalPredicted ) {
		std::pair< float, C_CSPlayer * > target{ 0.f, nullptr };

		for( int i = 1; i <= g_pGlobalVars->maxClients; ++i ) {
			const auto player = C_CSPlayer::GetPlayerByIndex( i );
			if( !player )
				continue;

			if( player->IsDead( ) || player->m_bGunGameImmunity( ) || player->IsTeammate( pLocal ) || g_Visuals.player_fading_alpha.at( i ) <= 0.4f )
				continue;

			const Vector vecOrigin = ( player->IsDormant( ) && g_ExtendedVisuals.m_arrOverridePlayers.at( i ).m_eOverrideType != EOverrideType::ESP_NONE ) ?
				g_ExtendedVisuals.m_arrOverridePlayers.at( i ).m_vecOrigin
				: player->m_vecOrigin( );

			const Vector center = vecOrigin;

			const Vector delta = center - vFinalPoint;

			if( m_iWeapIndex == WEAPON_HEGRENADE ) {
				if( delta.Length( ) <= 475.f ) {
					CGameTrace trace{ };
					CTraceFilter filter{ };
					filter.pSkip = player;

					g_pEngineTrace->TraceRay( Ray_t( vFinalPoint, center ), MASK_SHOT, ( ITraceFilter * )&filter, &trace );

					if( trace.hit_entity && trace.hit_entity == player ) {
						constexpr float a = 105.0f;
						constexpr float b = 25.0f;
						constexpr float c = 140.0f;

						// TODO: make this perfect / reverse it
						// damage is not perfectly calculated
						const float d = ( ( ( ( player->m_vecOrigin( ) ) - vFinalPoint ).Length( ) - b ) / c );
						float flDamage = a * exp( -d * d );
						flDamage = std::max( static_cast< int >( ceilf( g_GrenadeWarning.CSGOArmor( flDamage, player->m_ArmorValue( ) ) ) ), 0 );

						if( flDamage > target.first ) {
							target.first = flDamage;
							target.second = player;
						}
					}
				}
			}
			else if( m_iWeapIndex == WEAPON_MOLOTOV || m_iWeapIndex == WEAPON_INC ) {
				// is within damage radius?
				if( delta.Length( ) > 131.f )
					continue;

				bool bContinue = false;

				// loop thru all smoke positions on le map
				// if molotov lands in smoke then not dangerous)
				for( auto &smokes : g_Visuals.vecSmokesOrigin ) {
					if( smokes.second.IsZero( ) )
						continue;

					const Vector vecFinalMolotov = vFinalPoint;
					const Vector vecCenterSmoke = smokes.second;

					// LOL PYTHOGOREAN THEOREM IF DISTANCE BETWEEN 
					// X1 AND X2 IS SMALLER THAN RADIUS WE IN BOUNDS!!!!
					// THANK YOU GORDON RAMSEY MATH EDITION........
					if( vecCenterSmoke.Distance( vecFinalMolotov ) <= 144.f /*smoke radius np*/ ) {
						bContinue = true;
						break;
					}
				}

				if( bContinue )
					continue;

				// hardcoded bullshit /shrug
				target.first = 10.f;
				target.second = player;
			}
		}

		if( target.second ) {
			Vector2D screen{ };
			if( Render::Engine::WorldToScreen( vFinalPoint, screen ) ) {
				if( m_iWeapIndex == WEAPON_MOLOTOV || m_iWeapIndex == WEAPON_INC ) {
					if( bReached )
						Render::Engine::esp_bold_wpn.string( screen.x, screen.y + 5,
														 Color( 255, 0, 0, 180 ),
														 std::string( XorStr( "SPREAD" ) ), Render::Engine::ALIGN_CENTER );
				}
				else if( m_iWeapIndex == WEAPON_HEGRENADE ) {
					Render::Engine::esp_bold_wpn.string( screen.x, screen.y + 5,
													 target.first >= target.second->m_iHealth( ) ? Color( 255, 200, 60, 180 ) : Color( 255, 0, 0, 180 ),
													 std::string( XorStr( "DMG: " ) + std::to_string( ( int )target.first ) ), Render::Engine::ALIGN_CENTER );
				}
			}
		}
	}

	// from here on it's all just warning stuff
	if( m_bLocalPredicted || !g_Vars.esp.grenade_proximity_warning )
		return true;

	const Vector center = pLocal->GetAbsOrigin( );

	// https://i.imgur.com/whidDo4.png not sure, prob not related?
	if( center.IsZero( ) )
		return true;

	const float flTime = ( m_flExpireTime - g_pGlobalVars->curtime );
	const float flPercent = ( flTime / TICKS_TO_TIME( m_iTick ) );


	// compute nade string
	char nade_buffer[ 128 ] = {};
	sprintf( nade_buffer, XorStr( "FRAG (%.1fs)" ), flTime );
	const Vector delta = center - vFinalPoint;

	static CTraceFilter filter{ };
	CGameTrace trace;
	filter.pSkip = m_pNadeEntity;

	char damage_buffer[ 128 ] = {};
	sprintf( damage_buffer, XorStr( "UNSAFE" ) );
	bool safe_nade = true;

	float flWarningMultiplier = 0.f;
	if( m_iWeapIndex == WEAPON_HEGRENADE ) {
		if( delta.Length( ) <= 475.f ) {
			matrix3x4_t pBackupMatrix[ MAXSTUDIOBONES ];
			std::memcpy( pBackupMatrix, pLocal->m_CachedBoneData( ).Base( ), sizeof( matrix3x4_t ) * pLocal->m_CachedBoneData( ).Count( ) );

			// note - michal; yeah..... had some issues tracing to eyepos or origin (obv eyepos cos aa and obv origin cos nothing to hit half the time)
			// so i did the same stuff we do for the autowall bbox stuff and ye it seems to work perfectly XD
			pLocal->m_CachedBoneData( ).Base( )[ 8 ].MatrixSetColumn( pLocal->GetAbsOrigin( ), 3 );

			pLocal->m_iMostRecentModelBoneCounter( ) = *( int * )Engine::Displacement.Data.m_uModelBoneCounter;
			pLocal->m_BoneAccessor( ).m_ReadableBones = pLocal->m_BoneAccessor( ).m_WritableBones = 0xFFFFFFFF;
			pLocal->m_flLastBoneSetupTime( ) = FLT_MAX;

			g_pEngineTrace->TraceRay( Ray_t( vFinalPoint, pLocal->GetAbsOrigin( ) ), MASK_SHOT, ( ITraceFilter * )&filter, &trace );

			std::memcpy( pLocal->m_CachedBoneData( ).Base( ), pBackupMatrix, sizeof( matrix3x4_t ) * pLocal->m_CachedBoneData( ).Count( ) );

			if( trace.hit_entity && trace.hit_entity == pLocal ) {
				static float a = 105.0f;
				static float b = 25.0f;
				static float c = 140.0f;

				float d = ( ( ( ( pLocal->GetAbsOrigin( ) ) - vFinalPoint ).Length( ) - b ) / c );
				float flDamage = a * exp( -d * d );

				flDamage = std::max( static_cast< int >( ceilf( g_GrenadeWarning.CSGOArmor( flDamage, pLocal->m_ArmorValue( ) ) ) ), 0 );

				sprintf( damage_buffer, XorStr( "UNSAFE (-%i HP)" ), int( flDamage ) );
				safe_nade = false;

				// * 0.75 because we want some tolerance incase this nade will do
				// significant damage to us
				flWarningMultiplier = flDamage / std::max( 1.f, std::floor( pLocal->m_iHealth( ) * 0.75f ) );
				flWarningMultiplier = std::clamp<float>( flWarningMultiplier, 0.f, 1.f );
			}
		}
	}
	else if( ( m_iWeapIndex == WEAPON_MOLOTOV || m_iWeapIndex == WEAPON_INC ) ) {
		const float flFireRadius = 215.f;
		const float flDistance = vFinalPoint.Distance( pLocal->GetAbsOrigin( ) );
		// fire wont reach us (but it will spread so yea gg)
		if( flDistance > flFireRadius ) {
			flWarningMultiplier = 0.f;
		}
		else {
			flWarningMultiplier = 1.0f - ( flDistance / flFireRadius );
			safe_nade = false;
		}

		// loop thru all smoke positions on le map
		// if molotov lands in smoke then not dangerous)
		for( auto &smokes : g_Visuals.vecSmokesOrigin ) {
			if( smokes.second.IsZero( ) )
				continue;

			const Vector vecFinalMolotov = vFinalPoint;
			const Vector vecCenterSmoke = smokes.second;

			// LOL PYTHOGOREAN THEOREM IF DISTANCE BETWEEN 
			// X1 AND X2 IS SMALLER THAN RADIUS WE IN BOUNDS!!!!
			// THANK YOU GORDON RAMSEY MATH EDITION........
			if( vecCenterSmoke.Distance( vecFinalMolotov ) <= 144.f /*smoke radius np*/ ) {
				flWarningMultiplier = 0.f;
				safe_nade = true;
				break;
			}
		}
	}

	if( m_iWeapIndex == WEAPON_MOLOTOV || m_iWeapIndex == WEAPON_INC ) {
		sprintf( nade_buffer, XorStr( "FIRE (%.1fs)" ), flTime );

		if( !bReached ) {
			flWarningMultiplier = 0.f;
			safe_nade = true;
		}
	}

	flWarningMultiplier = std::clamp<float>( flWarningMultiplier, 0.f, 1.f );

	auto GetDamageColor = [ & ] ( ) {
		Color blendLOL = Color::Blend( Color( 150, 200, 60 ), Color::Red( ), flWarningMultiplier );

		if( safe_nade )
			blendLOL = Color( 150, 200, 60 );

		return blendLOL;
	};

	if( bPrevScreen ) {
		// c4 timer bar
		Render::Engine::RectFilled( vPrevScreen2D - Vector2D( 30, -10 ) - 1, Vector2D( 60, 2 ) + 2, Color( 0.f, 0.f, 0.f, 180 ) );
		Render::Engine::RectFilled( vPrevScreen2D - Vector2D( 30, -10 ), Vector2D( 60 * flPercent, 2 ), g_Vars.esp.grenade_proximity_warning_color.ToRegularColor( ).OverrideAlpha( 255, true ) );

		// main text
		Render::Engine::esp_bold_wpn.string( vPrevScreen2D.x, vPrevScreen2D.y, Color( 255, 255, 255, 240 ), nade_buffer, Render::Engine::ALIGN_CENTER );
		if( !safe_nade )
			Render::Engine::esp_bold_wpn.string( vPrevScreen2D.x, vPrevScreen2D.y - 10, Color( 255, 0, 0, 240 ), damage_buffer, Render::Engine::ALIGN_CENTER );
	}
#if 0

	if( flWarningMultiplier <= 0.f )
		return true;

	Vector2D screenPos;
	Vector vLocalOrigin = pLocal->GetAbsOrigin( );

	if( pLocal->IsDead( ) )
		vLocalOrigin = g_pInput->m_vecCameraOffset;

	// give some extra room for screen position to be off screen.
	const float flExtraRoom = iScreenWidth / 18.f;

	if( !Render::Engine::WorldToScreen( vFinalPoint, screenPos )
		|| screenPos.x < -flExtraRoom
		|| screenPos.x >( iScreenWidth + flExtraRoom )
		|| screenPos.y < -flExtraRoom
		|| screenPos.y >( iScreenHeight + flExtraRoom ) ) {
		QAngle angViewAngles;
		g_pEngine.Xor( )->GetViewAngles( angViewAngles );

		const Vector2D vecScreenCenter = Vector2D( iScreenWidth * .5f, iScreenHeight * .5f );
		const float flAngleYaw = DEG2RAD( angViewAngles.y - Math::CalcAngle( vLocalOrigin, vFinalPoint, true ).y - 90 );

		// note - michal;
		// untested this approach stuff with multiple nades rendering;
		// it shouldn't mess up but idk, i havent thought abt it enough
		// and it's a bit late but i'll test tomorrow/let beta users test
		float flWishPointX = ( vecScreenCenter.x + ( ( ( iScreenHeight - 60.f ) / 2 ) * std::clamp( 1.f - flWarningMultiplier, 0.25f, 0.85f ) ) * cos( flAngleYaw ) ) + 8.f;
		static float flNewPointX = flWishPointX;

		flNewPointX = GUI::Approach( flNewPointX, flWishPointX, g_pGlobalVars->frametime * 15.f );

		float flWishPointY = ( vecScreenCenter.y + ( ( ( iScreenHeight - 60.f ) / 2 ) * std::clamp( 1.f - flWarningMultiplier, 0.25f, 0.85f ) ) * sin( flAngleYaw ) ) + 8.f;
		static float flNewPointY = flWishPointY;

		flNewPointY = GUI::Approach( flNewPointY, flWishPointY, g_pGlobalVars->frametime * 15.f );

		Render::Engine::CircleFilled( flNewPointX, flNewPointY - ( vecIconSize.m_height / 2 ), 17.f, 130, Color::Blend( Color::Black( ), Color::Red( ), flWarningMultiplier ).OverrideAlpha( 200 ) );
		Render::Engine::cs_large.string( flNewPointX - ( vecIconSize.m_width / 2.f ), flNewPointY - vecIconSize.m_height, Color::White( ).OverrideAlpha( 25 ), strIcon );

		g_pSurface->DrawSetColor( Color::Black( ) );
		g_pSurface->DrawOutlinedCircle( flNewPointX, flNewPointY - ( vecIconSize.m_height / 2 ), 17.f, 130 );

		Render::Engine::SetClip( { flNewPointX - ( vecIconSize.m_width / 2.f ), flNewPointY - ( vecIconSize.m_height * flPercent ) }, Vector2D( vecIconSize.m_width, vecIconSize.m_height ) );
		Render::Engine::cs_large.string( flNewPointX - ( vecIconSize.m_width / 2.f ), flNewPointY - vecIconSize.m_height, Color::White( ).OverrideAlpha( 210 ), strIcon );
		Render::Engine::ResetClip( );
	}
#endif

	return true;
}

void GrenadeWarning::Run( C_BaseEntity *entity ) {
	auto &listNades = GetList( );

	static auto flLastServerTime = g_pGlobalVars->curtime;
	// let's not clear this too fast
	if( std::fabsf( g_pGlobalVars->curtime - flLastServerTime ) > 5.f ) {
		listNades.clear( );
	}

	auto pLocal = C_CSPlayer::GetLocalPlayer( );
	if( !pLocal )
		return;

	if( !entity )
		return;

	const auto ulHandle = ( unsigned long )entity->GetRefEHandle( ).Get( );

	if( ( entity->IsDormant( ) && listNades.find( ulHandle ) == listNades.end( ) ) )
		return;

	if( !entity->IsDormant( ) ) {
		const auto pClientClass = entity->GetClientClass( );
		if( !pClientClass || pClientClass->m_ClassID != CMolotovProjectile && pClientClass->m_ClassID != CBaseCSGrenadeProjectile /*&& pClientClass->m_ClassID != CDecoyProjectile && pClientClass->m_ClassID != CSmokeGrenadeProjectile*/ )
			return;

		if( pClientClass->m_ClassID == CBaseCSGrenadeProjectile ) {
			const auto pModel = entity->GetModel( );
			if( !pModel )
				return;

			const auto pStudiomodel = g_pModelInfo->GetStudiomodel( pModel );
			if( !pStudiomodel || std::string_view( pStudiomodel->szName ).find( XorStr( "fraggrenade" ) ) == std::string::npos )
				return;
		}


		// i'm so sry
		//static auto m_nExplodeEffectTickBeginOffset = Engine::g_PropManager.GetOffset( XorStr( "DT_BaseCSGrenadeProjectile" ), XorStr( "m_nExplodeEffectTickBegin" ) );
		static auto m_hThrowerOffset = Engine::g_PropManager.GetOffset( XorStr( "DT_BaseCSGrenadeProjectile" ), XorStr( "m_hThrower" ) );
		static auto m_vInitialVelocityOffset = Engine::g_PropManager.GetOffset( XorStr( "DT_BaseCSGrenadeProjectile" ), XorStr( "m_vInitialVelocity" ) );
		// reset here, on next (non dormant) run (so if nade goes dormant, we still draw the trail)
		listNades.erase( ulHandle );

		//auto m_nExplodeEffectTickBegin = *( int * )( uintptr_t( entity ) + m_nExplodeEffectTickBeginOffset );
		auto m_hThrower = *( CBaseHandle * )( uintptr_t( entity ) + m_hThrowerOffset );
		auto m_vInitialVelocity = *( Vector * )( uintptr_t( entity ) + m_vInitialVelocityOffset );

		if( !m_hThrower.IsValid( ) || !m_hThrower.Get( ) || ( ( ( C_BaseEntity * )m_hThrower.Get( ) )->m_iTeamNum( ) == pLocal->m_iTeamNum( ) && ( ( C_BaseEntity * )m_hThrower.Get( ) )->EntIndex( ) != g_pEngine->GetLocalPlayer( ) ) ) {
			listNades.erase( ulHandle );

			// will only remove hegrenades
			// molotov/fire will stay coz the current scope will never be hit
			const auto it = std::find_if( g_GrenadeWarning.m_vecEventNades.begin( ), g_GrenadeWarning.m_vecEventNades.end( ), [ ulHandle ] ( const auto &it ) -> bool { return std::get<2>( it ) == ulHandle; } );
			if( it != g_GrenadeWarning.m_vecEventNades.end( ) ) {
				g_GrenadeWarning.m_vecEventNades.erase( it );
			}

			return;
		}

		if( listNades.find( ulHandle ) == listNades.end( ) ) {
			float flSpawnTime = 0.f;

			for( auto &a : g_GrenadeWarning.m_vecEventNades ) {
				if( std::get<0>( a ) == ( ( C_BaseEntity * )m_hThrower.Get( ) )->EntIndex( ) ) {
					const float flDelta = std::fabsf( TICKS_TO_TIME( g_pGlobalVars->tickcount ) - std::get<1>( a ) );

					// flDelta is only for the initially decision, if this nade is the right one to take
					// as we don't clear m_vecEventNades it might take a wrong spawntime else (a too old one)
					// just filtering smh..
					if( ( std::get<2>( a ) == 0 && flDelta <= 10.f ) || std::get<2>( a ) == ulHandle ) {
						flSpawnTime = std::get<1>( a );

						std::get<2>( a ) = ulHandle;
					}
				}
			}

			int iWeaponIndex = WEAPON_HEGRENADE;
			switch( pClientClass->m_ClassID ) {
				case CMolotovProjectile:
					iWeaponIndex = WEAPON_MOLOTOV;
					break;
					//case CDecoyProjectile:
					//	iWeaponIndex = WEAPON_DECOY;
					//	break;
					//case CSmokeGrenadeProjectile:
					//	iWeaponIndex = WEAPON_SMOKEGRENADE;
					//	break;
			}


			bool bCanShift = false;
			if( reinterpret_cast< C_CSPlayer * >( m_hThrower.Get( ) )->EntIndex( ) == pLocal->EntIndex( ) )
				bCanShift = g_TickbaseController.CanShift( false, true ) && g_TickbaseController.m_bBreakingLC && g_Vars.rage.exploit && g_Vars.rage.double_tap_bind.enabled;

			const int nOffset = bCanShift ? g_TickbaseController.GetCorrection( ) : 0;

			listNades[ ulHandle ] = { ( C_CSPlayer * )m_hThrower.Get( ),
				iWeaponIndex,
				entity->m_vecOrigin( ), reinterpret_cast< C_CSPlayer * >( entity )->m_vecVelocity( ),
				flSpawnTime,
				/*g_pGlobalVars->tickcount - TIME_TO_TICKS( flSpawnTime )*/
				TIME_TO_TICKS( entity->m_flSimulationTime( ) ) - TIME_TO_TICKS( flSpawnTime ) + nOffset, entity };

			// last time we predicted smth..
			flLastServerTime = g_pGlobalVars->curtime;
		}
	}

	if( listNades.find( ulHandle ) != listNades.end( ) ) {
		// now draw it!
		listNades.at( ulHandle ).Draw( );
	}
}

void GrenadeWarning::PredictLocal( ) {
	// lazy way of resetting it all :) 
	m_data = { };

	if( !g_Vars.esp.grenade_prediction ) {
		m_data.m_Path.clear( );
		return;
	}

	const auto pLocal = C_CSPlayer::GetLocalPlayer( );
	if( !pLocal || pLocal->IsDead( ) )
		return;

	const auto pWeapon = reinterpret_cast< C_WeaponCSBaseGun * >( pLocal->m_hActiveWeapon( ).Get( ) );
	if( !pWeapon )
		return;

	auto pWeaponData = pWeapon->GetCSWeaponData( );
	if( !pWeaponData.IsValid( ) )
		return;

	if( pWeaponData->m_iWeaponType != WEAPONTYPE_GRENADE )
		return;

	if( !pWeapon->m_bPinPulled( ) )
		return;

	//if( pWeapon->m_fThrowTime( ) > 0.f )
	//	return;

	//static auto m_iPrimaryAmmoType = Engine::g_PropManager.GetOffset( XorStr( "DT_BaseCombatWeapon" ), XorStr( "m_iPrimaryAmmoType" ) );
	//static auto m_iAmmo = Engine::g_PropManager.GetOffset( XorStr( "DT_BasePlayer" ), XorStr( "m_iAmmo" ) );

	//int iAmmoType = *reinterpret_cast< int * >( uintptr_t( pWeapon ) + m_iPrimaryAmmoType );
	//if( iAmmoType < 0 || iAmmoType > 31 )
	//	return;

	//int *pAmmo = reinterpret_cast< int * >( uintptr_t( pLocal ) + m_iAmmo );
	//if( !pAmmo )
	//	return;

	//if( pAmmo[ iAmmoType ] <= 0 )
	//	return;

	// setup stuff.
	m_data.m_pOwner = pLocal;
	m_data.m_iWeapIndex = pWeapon->m_iItemDefinitionIndex( );
	m_data.m_bLocalPredicted = true;

	QAngle angThrow{ };

	// get view angles (direction)
	g_pEngine->GetViewAngles( angThrow );

	float pitch = angThrow.pitch;

	if( pitch <= 90.0f ) {
		if( pitch < -90.0f ) {
			pitch += 360.0f;
		}
	}
	else {
		pitch -= 360.0f;
	}
	float a = pitch - ( 90.0f - fabs( pitch ) ) * 10.0f / 90.0f;
	angThrow.pitch = a;

	// get ThrowVelocity from weapon files.
	float flVel = pWeaponData->m_flThrowVelocity * 0.9f;

	// clipped to [ 15, 750 ]
	Math::Clamp( flVel, 15.f, 750.f );

	//clamp the throw strength ranges just to be sure
	float flClampedThrowStrength = pWeapon->m_flThrowStrength( );
	flClampedThrowStrength = std::clamp( flClampedThrowStrength, 0.0f, 1.0f );

	flVel *= Lerp( flClampedThrowStrength, 0.3f, 1.0f );

	Vector vForward, vRight, vUp;
	vForward = angThrow.ToVectors( &vRight, &vUp );

	// danke DucaRii, ich liebe dich
	// dieses code snippet hat mir so sehr geholfen https://cdn.discordapp.com/attachments/755873329151475845/762297342623088640/unknown.png
	// thanks DucaRii, you are the greatest
	// loveeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee
	// kochamy DucaRii
	auto vecSrc = pLocal->GetAbsOrigin( ) + pLocal->m_vecViewOffset( );
	float off = Lerp( flClampedThrowStrength, -12.f, 0.0f );
	vecSrc.z += off;

	// Game calls UTIL_TraceHull here with hull and assigns vecSrc tr.endpos
	CGameTrace tr;
	Vector vecDest = vecSrc;
	vecDest = ( vecDest + vForward * 22.0f );
	TraceHull( vecSrc, vecDest, { -2.f, -2.f, -2.f }, { 2.f, 2.f, 2.f }, MASK_SOLID | CONTENTS_CURRENT_90, pLocal, COLLISION_GROUP_NONE, &tr );

	// after the hull trace it moves 6 units back along vForward
	// vecSrc = tr.endpos - vForward * 6
	Vector vecBack = vForward; vecBack *= 6.0f;
	vecSrc = tr.endpos;
	vecSrc -= vecBack;

	// kurwa fix for anti-aim micromovements (c) NICO
	Vector vecThrow{ };

	auto velocity = pLocal->m_vecVelocity( );
	if( velocity.Length2D( ) > 3.4 ) {
		vecThrow = velocity;
	}
	else {
		vecThrow.Init( );
	}

	vecThrow *= 1.25f;
	vecThrow += ( vForward * flVel );

	bool bCanShift = g_TickbaseController.CanShift( false, true ) && g_TickbaseController.m_bBreakingLC && g_Vars.rage.exploit && g_Vars.rage.double_tap_bind.enabled;
	const int nOffset = bCanShift ? g_TickbaseController.GetCorrection( ) : 0;
	m_data.Predict( vecSrc, vecThrow, g_pGlobalVars->curtime, nOffset );
}

void GrenadeWarning::DrawLocal( ) {

	if( !g_Vars.esp.grenade_prediction ) {
		m_data.m_Path.clear( );
		return;
	}

	PredictLocal( );

	if( !m_data.Draw( ) ) {
		return;
	}

	m_data.m_Path.clear( );
}
