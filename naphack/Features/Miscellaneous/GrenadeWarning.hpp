#pragma once
#include "../../pandora.hpp"
#include "../../SDK/Classes/Player.hpp"

#include "../../SDK/Displacement.hpp"

class GrenadeWarning {
public:
	__forceinline static void TraceHull( const Vector& src, const Vector& dst, const Vector& mins, const Vector& maxs, int mask, IHandleEntity* entity, int collision_group, CGameTrace* trace ) {
		std::uintptr_t filter[ 4 ] = {
			*reinterpret_cast< std::uintptr_t* >( Engine::Displacement.Function.m_TraceFilterSimple ),
			reinterpret_cast< std::uintptr_t >( entity ),
			collision_group,
			0
		};

		auto ray = Ray_t( );

		ray.Init( src, dst, mins, maxs );

		g_pEngineTrace->TraceRay( ray, mask, reinterpret_cast< CTraceFilter* >( &filter ), trace );
	}

	inline float CSGOArmor( float flDamage, int ArmorValue ) {
		float flArmorRatio = 0.5f;
		float flArmorBonus = 0.5f;
		if( ArmorValue > 0 ) {
			float flNew = flDamage * flArmorRatio;
			float flArmor = ( flDamage - flNew ) * flArmorBonus;

			if( flArmor > static_cast< float >( ArmorValue ) ) {
				flArmor = static_cast< float >( ArmorValue ) * ( 1.f / flArmorBonus );
				flNew = flDamage - flArmor;
			}

			flDamage = flNew;
		}
		return flDamage;
	}

	__forceinline static void TraceLine( const Vector& src, const Vector& dst, int mask, IHandleEntity* entity, int collision_group, CGameTrace* trace ) {
		std::uintptr_t filter[ 4 ] = {
			*reinterpret_cast< std::uintptr_t* >( Engine::Displacement.Function.m_TraceFilterSimple ),
			reinterpret_cast< std::uintptr_t >( entity ),
			collision_group,
			0
		};

		auto ray = Ray_t( );

		ray.Init( src, dst );

		g_pEngineTrace->TraceRay( ray, mask, reinterpret_cast< CTraceFilter* >( &filter ), trace );
	}

	template <class T>
	__forceinline T Lerp( float flPercent, T const& A, T const& B )
	{
		return A + ( B - A ) * flPercent;
	}

	struct GrenadeData_t {
		__forceinline GrenadeData_t( ) = default;

		__forceinline GrenadeData_t( C_CSPlayer* owner, int index, const Vector& origin, const Vector& velocity, float throw_time, int offset, C_BaseEntity* entity ) : GrenadeData_t( ) {
			m_pOwner = owner;
			m_pNadeEntity = entity;
			m_iWeapIndex = index;

			Predict( origin, velocity, throw_time, offset );
		}

		bool PhysicsSimulate( );

		void PhysicsTraceEntity( const Vector& src, const Vector& dst, std::uint32_t mask, CGameTrace& trace );

		void PhysicsPushEntity( const Vector& push, CGameTrace& trace );

		void PerformFlyCollision( CGameTrace& trace );

		void Think( );

		__forceinline void Detonate( const bool bBounced ) {
			m_bDetonated = true;

			UpdatePath( bBounced );
		}

		__forceinline void UpdatePath( const bool bBounced ) {
			m_iLastUpdateTick = m_iTick;

			m_Path.emplace_back( m_vecOrigin, bBounced );
		}

		void Predict( const Vector& origin, const Vector& velocity, float throw_time, int offset );

		bool Draw( ) const;

		bool m_bDetonated{ }, m_bLocalPredicted{ false };
		C_CSPlayer* m_pOwner{ };
		Vector m_vecOrigin{ }, m_vecVelocity{ };
		IClientEntity* m_pLastHitEntity{ };
		C_BaseEntity* m_pNadeEntity{ };
		int m_iCollisionGroup{ };
		float m_flDetonationTime{ }, m_flExpireTime{ };
		int m_iWeapIndex{ }, m_iTick{ }, m_iNextThinkTick{ }, m_iLastUpdateTick{ }, m_nBounces{ };
		std::vector< std::pair< Vector, bool > > m_Path{ };
	} m_data{ };

	std::unordered_map< unsigned long, GrenadeData_t > m_List{ };
public:
	std::unordered_map< unsigned long, GrenadeData_t >& GetList( ) {
		return m_List;
	}

	std::vector<std::tuple<int, float, unsigned long>> m_vecEventNades;

	void Run( C_BaseEntity* entity );

	void PredictLocal( );
	void DrawLocal( );
};

extern GrenadeWarning g_GrenadeWarning;