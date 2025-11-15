#pragma once
#include "../../SDK/sdk.hpp"
#include "../../SDK/Classes/Player.hpp"

class Sequence {
public:
	int   m_nInReliableState;
	int   m_nInSequenceNr;

public:
	__forceinline Sequence( ) : m_nInReliableState{}, m_nInSequenceNr{} {};
	__forceinline Sequence( int state, int seq ) : m_nInReliableState{ state }, m_nInSequenceNr{ seq } {};
};

class FakePing {
public:
	__forceinline void UpdateCanShift( float flTime ) {
		if( m_flLastTimeShift != 0.f ) {
			if( fabsf( m_flLastTimeShift - flTime ) > 5.f ) {
				m_bCanTimeShift = true;
			}
		}
	}

	bool m_bDidTimeShift = false;
	bool m_bCanTimeShift = true;
	float m_flLastTimeShift = 0.f;

	void GetTargetLatency( );
	void OnSendDatagram( INetChannel *pNetChannel );
	void UpdateIncomingSequences( INetChannel *pNetChannel );

	float m_flLastIncreaseTime;
	int m_nLastSequenceNr;
	bool m_bAllowFakePing;
	int m_nLastTickSkipped;
	bool m_bInProcessPacket;
	bool m_bFlippedState;
	int m_nLastInReliableState;
	int m_nTargetLatency;

	std::deque< Sequence > m_Sequences;
};

extern FakePing g_FakePing;