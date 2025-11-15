#include "FakePing.hpp"
#include "Animations.hpp"
#include "EnginePrediction.hpp"

FakePing g_FakePing;

void FakePing::GetTargetLatency( ) {
	// the reason we want to do this, is because if we are let's say not using ping spike, then toggle 800 pingsike, we are trying to send too much.
	// this, we could get prediction errors that would fullstop us etc.
	// this is negated by just slowly adding to the target value, and using that.

	// get our values.
	bool bPingSpikeEnabled = g_Vars.misc.extended_backtrack || ( g_Vars.misc.ping_spike && g_Vars.misc.ping_spike_key.enabled );
	int nPingSpikeAmount = ( g_Vars.misc.ping_spike && g_Vars.misc.ping_spike_key.enabled ) ? g_Vars.misc.ping_spike_amount : 150.f;

	// ping spike is not enabled, just set it to 0.
	if( !bPingSpikeEnabled ) {
		m_nTargetLatency = 0;

		// we are done here.
		return;
	}

	// our target latency is less than the desired value, increase value.
	if( m_nTargetLatency < nPingSpikeAmount )
		m_nTargetLatency += 5;

	// our target latency is more than the desired value, decrease value.
	else if( m_nTargetLatency > nPingSpikeAmount )
		m_nTargetLatency -= 5;

	// clamp this just to be sure.
	m_nTargetLatency = std::clamp<int>( m_nTargetLatency, 0, nPingSpikeAmount );
	// printf( "target lat: %d, %d\n", g_FakePing.m_nTargetLatency, nPingSpikeAmount );
}

void FakePing::OnSendDatagram( INetChannel *pNetChannel ) {
	if( !pNetChannel || m_Sequences.empty( ) ) {
		return;
	}

	if( g_Animations.m_flOutgoingLatency < m_nTargetLatency / 1000.0f ) {
		auto v13 = pNetChannel->m_nInSequenceNr - TIME_TO_TICKS( m_nTargetLatency / 1000.0f - g_Animations.m_flOutgoingLatency );
		pNetChannel->m_nInSequenceNr = v13;

		// iterate sequences.
		for( auto &s : m_Sequences ) {
			if( s.m_nInSequenceNr != v13 )
				continue;

			pNetChannel->m_nInReliableState = s.m_nInReliableState;
		}
	}
}

void FakePing::UpdateIncomingSequences( INetChannel *pNetChannel ) {
	if( !pNetChannel )
		return;

	// store new stuff.
	m_Sequences.emplace_back( pNetChannel->m_nInReliableState, pNetChannel->m_nInSequenceNr );

	for( auto it = m_Sequences.begin( ); it != m_Sequences.end( ); ++it ) {
		auto delta = abs( pNetChannel->m_nInSequenceNr - it->m_nInSequenceNr );
		if( delta > 128 ) {
			it = m_Sequences.erase( it );
		}
	}
}
