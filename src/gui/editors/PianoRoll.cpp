/*
 * PianoRoll.cpp - implementation of piano roll which is used for actual
 *                  writing of melodies
 *
 * Copyright (c) 2004-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
 * Copyright (c) 2008 Andrew Kelley <superjoe30/at/gmail/dot/com>
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

#include "PianoRoll.h"

#include <QApplication>
#include <QClipboard>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QMdiArea>
#include <QPainter>
#include <QPointer>
#include <QScrollBar>
#include <QStyleOption>

#ifndef __USE_XOPEN
#define __USE_XOPEN
#endif

#include <math.h>
#include <utility>

#include "AutomationEditor.h"
#include "ActionGroup.h"
#include "ConfigManager.h"
#include "BBTrackContainer.h"
#include "Clipboard.h"
#include "ComboBox.h"
#include "debug.h"
#include "DetuningHelper.h"
#include "embed.h"
#include "GuiApplication.h"
#include "gui_templates.h"
#include "InstrumentTrack.h"
#include "MainWindow.h"
#include "Pattern.h"
#include "SongEditor.h"
#include "stdshims.h"
#include "TextFloat.h"
#include "TimeLineWidget.h"
#include "StepRecorderWidget.h"


using std::move;

typedef AutomationPattern::timeMap timeMap;


// some constants...
const int INITIAL_PIANOROLL_WIDTH = 860;
const int INITIAL_PIANOROLL_HEIGHT = 480;

const int SCROLLBAR_SIZE = 12;
const int PIANO_X = 0;

const int WHITE_KEY_WIDTH = 64;
const int BLACK_KEY_WIDTH = 41;

const int DEFAULT_KEY_LINE_HEIGHT = 12;
const int DEFAULT_CELL_WIDTH = 12;


const int NOTE_EDIT_RESIZE_BAR = 6;
const int NOTE_EDIT_MIN_HEIGHT = 50;
const int KEY_AREA_MIN_HEIGHT = 100;
const int PR_BOTTOM_MARGIN = SCROLLBAR_SIZE;
const int PR_TOP_MARGIN = 16;
const int PR_RIGHT_MARGIN = SCROLLBAR_SIZE;


// width of area used for resizing (the grip at the end of a note)
const int RESIZE_AREA_WIDTH = 9;

// width of line for setting volume/panning of note
const int NOTE_EDIT_LINE_WIDTH = 3;

// key where to start
const int INITIAL_START_KEY = Key_C + Octave_4 * KeysPerOctave;

// number of each note to provide in quantization and note lengths
const int NUM_EVEN_LENGTHS = 6;
const int NUM_TRIPLET_LENGTHS = 5;



QPixmap * PianoRoll::s_whiteKeySmallPm = NULL;
QPixmap * PianoRoll::s_whiteKeySmallPressedPm = NULL;
QPixmap * PianoRoll::s_whiteKeyBigPm = NULL;
QPixmap * PianoRoll::s_whiteKeyBigPressedPm = NULL;
QPixmap * PianoRoll::s_blackKeyPm = NULL;
QPixmap * PianoRoll::s_blackKeyPressedPm = NULL;
QPixmap * PianoRoll::s_toolDraw = NULL;
QPixmap * PianoRoll::s_toolErase = NULL;
QPixmap * PianoRoll::s_toolSelect = NULL;
QPixmap * PianoRoll::s_toolMove = NULL;
QPixmap * PianoRoll::s_toolOpen = NULL;

TextFloat * PianoRoll::s_textFloat = NULL;

static QString s_noteStrings[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

static QString getNoteString( int key )
{
	return s_noteStrings[key % 12] + QString::number( static_cast<int>( key / KeysPerOctave ) );
}

// used for drawing of piano
PianoRoll::PianoRollKeyTypes PianoRoll::prKeyOrder[] =
{
	PR_WHITE_KEY_SMALL, PR_BLACK_KEY, PR_WHITE_KEY_BIG, PR_BLACK_KEY,
	PR_WHITE_KEY_SMALL, PR_WHITE_KEY_SMALL, PR_BLACK_KEY, PR_WHITE_KEY_BIG,
	PR_BLACK_KEY, PR_WHITE_KEY_BIG, PR_BLACK_KEY, PR_WHITE_KEY_SMALL
} ;


const int DEFAULT_PR_PPB = DEFAULT_CELL_WIDTH * DefaultStepsPerBar;

const QVector<double> PianoRoll::m_zoomLevels =
		{0.125f, 0.25f, 0.5f, 1.0f, 1.5f, 2.0f, 4.0f, 8.0f};

const QVector<double> PianoRoll::m_zoomYLevels =
		{0.25f, 0.5f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 4.0f};


PianoRoll::PianoRoll() :
	m_nemStr( QVector<QString>() ),
	m_noteEditMenu( NULL ),
	m_semiToneMarkerMenu( NULL ),
	m_zoomingModel(),
	m_zoomingYModel(),
	m_quantizeModel(),
	m_noteLenModel(),
	m_scaleModel(),
	m_chordModel(),
	m_pattern( NULL ),
	m_currentPosition(),
	m_recording( false ),
	m_currentNote( NULL ),
	m_action( ActionNone ),
	m_noteEditMode( NoteEditVolume ),
	m_moveBoundaryLeft( 0 ),
	m_moveBoundaryTop( 0 ),
	m_moveBoundaryRight( 0 ),
	m_moveBoundaryBottom( 0 ),
	m_mouseDownKey( 0 ),
	m_mouseDownTick( 0 ),
	m_lastMouseX( 0 ),
	m_lastMouseY( 0 ),
	m_oldNotesEditHeight( 100 ),
	m_notesEditHeight( 100 ),
	m_ppb( DEFAULT_PR_PPB ),
	m_keyLineHeight(DEFAULT_KEY_LINE_HEIGHT),
	m_octaveHeight(m_keyLineHeight * KeysPerOctave),
	m_whiteKeySmallHeight(round(m_keyLineHeight * 1.5)),
	m_whiteKeyBigHeight(m_keyLineHeight * 2),
	m_blackKeyHeight(round(m_keyLineHeight * 1.3333)),
	m_lenOfNewNotes( MidiTime( 0, DefaultTicksPerBar/4 ) ),
	m_lastNoteVolume( DefaultVolume ),
	m_lastNotePanning( DefaultPanning ),
	m_startKey( INITIAL_START_KEY ),
	m_lastKey( 0 ),
	m_editMode( ModeDraw ),
	m_ctrlMode( ModeDraw ),
	m_mouseDownRight( false ),
	m_scrollBack( false ),
	m_stepRecorderWidget(this, DEFAULT_PR_PPB, PR_TOP_MARGIN, PR_BOTTOM_MARGIN + m_notesEditHeight, WHITE_KEY_WIDTH, 0),
	m_stepRecorder(*this, m_stepRecorderWidget),
	m_barLineColor( 0, 0, 0 ),
	m_beatLineColor( 0, 0, 0 ),
	m_lineColor( 0, 0, 0 ),
	m_noteModeColor( 0, 0, 0 ),
	m_noteColor( 0, 0, 0 ),
	m_ghostNoteColor( 0, 0, 0 ),
	m_ghostNoteTextColor( 0, 0, 0 ),
	m_barColor( 0, 0, 0 ),
	m_selectedNoteColor( 0, 0, 0 ),
	m_textColor( 0, 0, 0 ),
	m_textColorLight( 0, 0, 0 ),
	m_textShadow( 0, 0, 0 ),
	m_markedSemitoneColor( 0, 0, 0 ),
	m_noteOpacity( 255 ),
	m_ghostNoteOpacity( 255 ),
	m_noteBorders( true ),
	m_ghostNoteBorders( true ),
	m_backgroundShade( 0, 0, 0 )
{
	// gui names of edit modes
	m_nemStr.push_back( tr( "Note Velocity" ) );
	m_nemStr.push_back( tr( "Note Panning" ) );

	m_noteEditMenu = new QMenu( this );
	m_noteEditMenu->clear();
	for( int i = 0; i < m_nemStr.size(); ++i )
	{
		QAction * act = new QAction( m_nemStr.at(i), this );
		connect( act, &QAction::triggered, [this, i](){ changeNoteEditMode(i); } );
		m_noteEditMenu->addAction( act );
	}

	m_semiToneMarkerMenu = new QMenu( this );

	QAction* markSemitoneAction = new QAction( tr("Mark/unmark current semitone"), this );
	QAction* markAllOctaveSemitonesAction = new QAction( tr("Mark/unmark all corresponding octave semitones"), this );
	QAction* markScaleAction = new QAction( tr("Mark current scale"), this );
	QAction* markChordAction = new QAction( tr("Mark current chord"), this );
	QAction* unmarkAllAction = new QAction( tr("Unmark all"), this );
	QAction* copyAllNotesAction = new QAction( tr("Select all notes on this key"), this);

	connect( markSemitoneAction, &QAction::triggered, [this](){ markSemiTone(stmaMarkCurrentSemiTone); });
	connect( markAllOctaveSemitonesAction, &QAction::triggered, [this](){ markSemiTone(stmaMarkAllOctaveSemiTones); });
	connect( markScaleAction, &QAction::triggered, [this](){ markSemiTone(stmaMarkCurrentScale); });
	connect( markChordAction, &QAction::triggered, [this](){ markSemiTone(stmaMarkCurrentChord); });
	connect( unmarkAllAction, &QAction::triggered, [this](){ markSemiTone(stmaUnmarkAll); });
	connect( copyAllNotesAction, &QAction::triggered, [this](){ markSemiTone(stmaCopyAllNotesOnKey); });

	markScaleAction->setEnabled( false );
	markChordAction->setEnabled( false );

	connect( this, SIGNAL(semiToneMarkerMenuScaleSetEnabled(bool)), markScaleAction, SLOT(setEnabled(bool)) );
	connect( this, SIGNAL(semiToneMarkerMenuChordSetEnabled(bool)), markChordAction, SLOT(setEnabled(bool)) );

	m_semiToneMarkerMenu->addAction( markSemitoneAction );
	m_semiToneMarkerMenu->addAction( markAllOctaveSemitonesAction );
	m_semiToneMarkerMenu->addAction( markScaleAction );
	m_semiToneMarkerMenu->addAction( markChordAction );
	m_semiToneMarkerMenu->addAction( unmarkAllAction );
	m_semiToneMarkerMenu->addAction( copyAllNotesAction );

	// init pixmaps
	if( s_whiteKeySmallPm == NULL )
	{
		s_whiteKeySmallPm = new QPixmap( embed::getIconPixmap(
						"pr_white_key_small" ) );
	}
	if( s_whiteKeySmallPressedPm == NULL )
	{
		s_whiteKeySmallPressedPm = new QPixmap( embed::getIconPixmap(
						"pr_white_key_small_pressed" ) );
	}
	if( s_whiteKeyBigPm == NULL )
	{
		s_whiteKeyBigPm = new QPixmap( embed::getIconPixmap(
							"pr_white_key_big" ) );
	}
	if( s_whiteKeyBigPressedPm == NULL )
	{
		s_whiteKeyBigPressedPm = new QPixmap( embed::getIconPixmap(
							"pr_white_key_big_pressed" ) );
	}
	if( s_blackKeyPm == NULL )
	{
		s_blackKeyPm = new QPixmap( embed::getIconPixmap(
							"pr_black_key" ) );
	}
	if( s_blackKeyPressedPm == NULL )
	{
		s_blackKeyPressedPm = new QPixmap( embed::getIconPixmap(
							"pr_black_key_pressed" ) );
	}
	if( s_toolDraw == NULL )
	{
		s_toolDraw = new QPixmap( embed::getIconPixmap( "edit_draw" ) );
	}
	if( s_toolErase == NULL )
	{
		s_toolErase= new QPixmap( embed::getIconPixmap( "edit_erase" ) );
	}
	if( s_toolSelect == NULL )
	{
		s_toolSelect = new QPixmap( embed::getIconPixmap( "edit_select" ) );
	}
	if( s_toolMove == NULL )
	{
		s_toolMove = new QPixmap( embed::getIconPixmap( "edit_move" ) );
	}
	if( s_toolOpen == NULL )
	{
		s_toolOpen = new QPixmap( embed::getIconPixmap( "automation" ) );
	}

	// init text-float
	if( s_textFloat == NULL )
	{
		s_textFloat = new TextFloat;
	}

	setAttribute( Qt::WA_OpaquePaintEvent, true );

	// add time-line
	m_timeLine = new TimeLineWidget( WHITE_KEY_WIDTH, 0, m_ppb,
					Engine::getSong()->getPlayPos(
						Song::Mode_PlayPattern ),
						m_currentPosition,
						Song::Mode_PlayPattern, this );
	connect( this, SIGNAL( positionChanged( const MidiTime & ) ),
		m_timeLine, SLOT( updatePosition( const MidiTime & ) ) );
	connect( m_timeLine, SIGNAL( positionChanged( const MidiTime & ) ),
			this, SLOT( updatePosition( const MidiTime & ) ) );

	//update timeline when in step-recording mode
	connect( &m_stepRecorderWidget, SIGNAL( positionChanged( const MidiTime & ) ),
			this, SLOT( updatePositionStepRecording( const MidiTime & ) ) );

	// update timeline when in record-accompany mode
	connect( Engine::getSong()->getPlayPos( Song::Mode_PlaySong ).m_timeLine,
				SIGNAL( positionChanged( const MidiTime & ) ),
			this,
			SLOT( updatePositionAccompany( const MidiTime & ) ) );
	// TODO
/*	connect( engine::getSong()->getPlayPos( Song::Mode_PlayBB ).m_timeLine,
				SIGNAL( positionChanged( const MidiTime & ) ),
			this,
			SLOT( updatePositionAccompany( const MidiTime & ) ) );*/

	removeSelection();

	// init scrollbars
	m_leftRightScroll = new QScrollBar( Qt::Horizontal, this );
	m_leftRightScroll->setSingleStep( 1 );
	connect( m_leftRightScroll, SIGNAL( valueChanged( int ) ), this,
						SLOT( horScrolled( int ) ) );

	m_topBottomScroll = new QScrollBar( Qt::Vertical, this );
	m_topBottomScroll->setSingleStep( 1 );
	m_topBottomScroll->setPageStep( 20 );
	connect( m_topBottomScroll, SIGNAL( valueChanged( int ) ), this,
						SLOT( verScrolled( int ) ) );

	// setup zooming-stuff
	for( float const & zoomLevel : m_zoomLevels )
	{
		m_zoomingModel.addItem( QString( "%1\%" ).arg( zoomLevel * 100 ) );
	}
	m_zoomingModel.setValue( m_zoomingModel.findText( "100%" ) );
	connect( &m_zoomingModel, SIGNAL( dataChanged() ),
					this, SLOT( zoomingChanged() ) );

	// zoom y
	for (float const & zoomLevel : m_zoomYLevels)
	{
		m_zoomingYModel.addItem(QString( "%1\%" ).arg(zoomLevel * 100));
	}
	m_zoomingYModel.setValue(m_zoomingYModel.findText("100%"));
	connect(&m_zoomingYModel, SIGNAL(dataChanged()),
					this, SLOT(zoomingYChanged()));

	// Set up quantization model
	m_quantizeModel.addItem( tr( "Note lock" ) );
	for( int i = 0; i <= NUM_EVEN_LENGTHS; ++i )
	{
		m_quantizeModel.addItem( "1/" + QString::number( 1 << i ) );
	}
	for( int i = 0; i < NUM_TRIPLET_LENGTHS; ++i )
	{
		m_quantizeModel.addItem( "1/" + QString::number( (1 << i) * 3 ) );
	}
	m_quantizeModel.addItem( "1/192" );
	m_quantizeModel.setValue( m_quantizeModel.findText( "1/16" ) );

	connect( &m_quantizeModel, SIGNAL( dataChanged() ),
					this, SLOT( quantizeChanged() ) );

	// Set up note length model
	m_noteLenModel.addItem( tr( "Last note" ),
					make_unique<PixmapLoader>( "edit_draw" ) );
	const QString pixmaps[] = { "whole", "half", "quarter", "eighth",
						"sixteenth", "thirtysecond", "triplethalf",
						"tripletquarter", "tripleteighth",
						"tripletsixteenth", "tripletthirtysecond" } ;

	for( int i = 0; i < NUM_EVEN_LENGTHS; ++i )
	{
		auto loader = make_unique<PixmapLoader>( "note_" + pixmaps[i] );
		m_noteLenModel.addItem( "1/" + QString::number( 1 << i ), ::move(loader) );
	}
	for( int i = 0; i < NUM_TRIPLET_LENGTHS; ++i )
	{
		auto loader = make_unique<PixmapLoader>( "note_" + pixmaps[i+NUM_EVEN_LENGTHS] );
		m_noteLenModel.addItem( "1/" + QString::number( (1 << i) * 3 ), ::move(loader) );
	}
	m_noteLenModel.setValue( 0 );

	// Note length change can cause a redraw if Q is set to lock
	connect( &m_noteLenModel, SIGNAL( dataChanged() ),
					this, SLOT( noteLengthChanged() ) );

	// Set up scale model
	const InstrumentFunctionNoteStacking::ChordTable& chord_table =
			InstrumentFunctionNoteStacking::ChordTable::getInstance();

	m_scaleModel.addItem( tr("No scale") );
	for( const InstrumentFunctionNoteStacking::Chord& chord : chord_table )
	{
		if( chord.isScale() )
		{
			m_scaleModel.addItem( chord.getName() );
		}
	}

	m_scaleModel.setValue( 0 );
	// change can update m_semiToneMarkerMenu
	connect( &m_scaleModel, SIGNAL( dataChanged() ),
						this, SLOT( updateSemiToneMarkerMenu() ) );

	// Set up chord model
	m_chordModel.addItem( tr("No chord") );
	for( const InstrumentFunctionNoteStacking::Chord& chord : chord_table )
	{
		if( ! chord.isScale() )
		{
			m_chordModel.addItem( chord.getName() );
		}
	}

	m_chordModel.setValue( 0 );

	// change can update m_semiToneMarkerMenu
	connect( &m_chordModel, SIGNAL( dataChanged() ),
					this, SLOT( updateSemiToneMarkerMenu() ) );

	setFocusPolicy( Qt::StrongFocus );
	setFocus();
	setMouseTracking( true );

	connect( &m_scaleModel, SIGNAL( dataChanged() ),
					this, SLOT( updateSemiToneMarkerMenu() ) );

	connect( Engine::getSong(), SIGNAL( timeSignatureChanged( int, int ) ),
						this, SLOT( update() ) );

	//connection for selecion from timeline
	connect( m_timeLine, SIGNAL( regionSelectedFromPixels( int, int ) ),
			this, SLOT( selectRegionFromPixels( int, int ) ) );

	m_stepRecorder.initialize();
}



void PianoRoll::reset()
{
	m_lastNoteVolume = DefaultVolume;
	m_lastNotePanning = DefaultPanning;
	clearGhostPattern();
}

void PianoRoll::showTextFloat(const QString &text, const QPoint &pos, int timeout)
{
	s_textFloat->setText( text );
	// show the float, offset slightly so as to not obscure anything
	s_textFloat->moveGlobal( this, pos + QPoint(4, 16) );
	if (timeout == -1)
	{
		s_textFloat->show();
	}
	else
	{
		s_textFloat->setVisibilityTimeOut( timeout );
	}
}


void PianoRoll::showVolTextFloat(volume_t vol, const QPoint &pos, int timeout)
{
	//! \todo display velocity for MIDI-based instruments
	// possibly dBFS values too? not sure if it makes sense for note volumes...
	showTextFloat( tr("Velocity: %1%").arg( vol ), pos, timeout );
}


void PianoRoll::showPanTextFloat(panning_t pan, const QPoint &pos, int timeout)
{
	QString text;
	if( pan < 0 )
	{
		text = tr("Panning: %1% left").arg( qAbs( pan ) );
	}
	else if( pan > 0 )
	{
		text = tr("Panning: %1% right").arg( qAbs( pan ) );
	}
	else
	{
		text = tr("Panning: center");
	}
	showTextFloat( text, pos, timeout );
}



void PianoRoll::changeNoteEditMode( int i )
{
	m_noteEditMode = (NoteEditMode) i;
	repaint();
}


void PianoRoll::markSemiTone( int i )
{
	const int key = m_pianoKeySelected;
	const InstrumentFunctionNoteStacking::Chord * chord = nullptr;

	switch( static_cast<SemiToneMarkerAction>( i ) )
	{
		case stmaUnmarkAll:
			m_markedSemiTones.clear();
			break;
		case stmaMarkCurrentSemiTone:
		{
			QList<int>::iterator it = std::find( m_markedSemiTones.begin(), m_markedSemiTones.end(), key );
			if( it != m_markedSemiTones.end() )
			{
				m_markedSemiTones.erase( it );
			}
			else
			{
				m_markedSemiTones.push_back( key );
			}
			break;
		}
		case stmaMarkAllOctaveSemiTones:
		{
			QList<int> aok = getAllOctavesForKey(key);

			if ( m_markedSemiTones.contains(key) )
			{
				// lets erase all of the ones that match this by octave
				QList<int>::iterator i;
				for (int ix = 0; ix < aok.size(); ++ix)
				{
					i = std::find(m_markedSemiTones.begin(), m_markedSemiTones.end(), aok.at(ix));
					if (i != m_markedSemiTones.end())
					{
						m_markedSemiTones.erase(i);
					}
				}
			}
			else
			{
				// we should add all of the ones that match this by octave
				m_markedSemiTones.append(aok);
			}

			break;
		}
		case stmaMarkCurrentScale:
			chord = & InstrumentFunctionNoteStacking::ChordTable::getInstance()
					.getScaleByName( m_scaleModel.currentText() );
		case stmaMarkCurrentChord:
		{
			if( ! chord )
			{
				chord = & InstrumentFunctionNoteStacking::ChordTable::getInstance()
						.getChordByName( m_chordModel.currentText() );
			}

			if( chord->isEmpty() )
			{
				break;
			}
			else if( chord->isScale() )
			{
				m_markedSemiTones.clear();
			}

			const int first = chord->isScale() ? 0 : key;
			const int last = chord->isScale() ? NumKeys : key + chord->last();
			const int cap = ( chord->isScale() || chord->last() == 0 ) ? KeysPerOctave : chord->last();

			for( int i = first; i <= last; i++ )
			{
			  if( chord->hasSemiTone( ( i + cap - ( key % cap ) ) % cap ) )
				{
					m_markedSemiTones.push_back( i );
				}
			}
			break;
		}
		case stmaCopyAllNotesOnKey:
		{
			selectNotesOnKey();
			break;
		}
		default:
			;
	}

	std::sort( m_markedSemiTones.begin(), m_markedSemiTones.end(), std::greater<int>() );
	QList<int>::iterator new_end = std::unique( m_markedSemiTones.begin(), m_markedSemiTones.end() );
	m_markedSemiTones.erase( new_end, m_markedSemiTones.end() );
}


PianoRoll::~PianoRoll()
{
}


void PianoRoll::setGhostPattern( Pattern* newPattern )
{
	// Expects a pointer to a pattern or nullptr.
	m_ghostNotes.clear();
	if( newPattern != nullptr )
	{
		for( Note *note : newPattern->notes() )
		{
			Note * new_note = new Note( note->length(), note->pos(), note->key() );
			m_ghostNotes.push_back( new_note );
		}
		emit ghostPatternSet( true );
	}
}


void PianoRoll::loadGhostNotes( const QDomElement & de )
{
	// Load ghost notes from DOM element.
	if( de.isElement() )
	{
		QDomNode node = de.firstChild();
		while( !node.isNull() )
		{
			Note * n = new Note;
			n->restoreState( node.toElement() );
			m_ghostNotes.push_back( n );
			node = node.nextSibling();
		}
		emit ghostPatternSet( true );
	}
}


void PianoRoll::clearGhostPattern()
{
	setGhostPattern( nullptr );
	emit ghostPatternSet( false );
	update();
}


void PianoRoll::loadMarkedSemiTones(const QDomElement & de)
{
	// clear marked semitones to prevent leftover marks
	m_markedSemiTones.clear();
	if (de.isElement())
	{
		QDomNode node = de.firstChild();
		while (!node.isNull())
		{
			bool ok;
			int key = node.toElement().attribute(
				QString("key"), QString("-1")).toInt(&ok, 10);
			if (ok && key >= 0)
			{
				m_markedSemiTones.append(key);
			}
			node = node.nextSibling();
		}
	}
	// from markSemiTone, required otherwise marks will not show
	std::sort(m_markedSemiTones.begin(), m_markedSemiTones.end(), std::greater<int>());
	QList<int>::iterator new_end = std::unique(m_markedSemiTones.begin(), m_markedSemiTones.end());
	m_markedSemiTones.erase(new_end, m_markedSemiTones.end());
}


void PianoRoll::setCurrentPattern( Pattern* newPattern )
{
	if( hasValidPattern() )
	{
		m_pattern->instrumentTrack()->disconnect( this );
	}

	// force the song-editor to stop playing if it played pattern before
	if( Engine::getSong()->isPlaying() &&
		Engine::getSong()->playMode() == Song::Mode_PlayPattern )
	{
		Engine::getSong()->playPattern( NULL );
	}

	if(m_stepRecorder.isRecording())
	{
		m_stepRecorder.stop();
	}

	// set new data
	m_pattern = newPattern;
	m_currentPosition = 0;
	m_currentNote = NULL;
	m_startKey = INITIAL_START_KEY;

	m_stepRecorder.setCurrentPattern(newPattern);

	if( ! hasValidPattern() )
	{
		//resizeEvent( NULL );

		update();
		emit currentPatternChanged();
		return;
	}

	m_leftRightScroll->setValue( 0 );

	// determine the central key so that we can scroll to it
	int central_key = 0;
	int total_notes = 0;
	for( const Note *note : m_pattern->notes() )
	{
		if( note->length() > 0 )
		{
			central_key += note->key();
			++total_notes;
		}
	}

	if( total_notes > 0 )
	{
		central_key = central_key / total_notes -
				( KeysPerOctave * NumOctaves - m_totalKeysToScroll ) / 2;
		m_startKey = qBound( 0, central_key, NumOctaves * KeysPerOctave );
	}

	// resizeEvent() does the rest for us (scrolling, range-checking
	// of start-notes and so on...)
	resizeEvent( NULL );

	// make sure to always get informed about the pattern being destroyed
	connect( m_pattern, SIGNAL( destroyedPattern( Pattern* ) ), this, SLOT( hidePattern( Pattern* ) ) );

	connect( m_pattern->instrumentTrack(), SIGNAL( midiNoteOn( const Note& ) ), this, SLOT( startRecordNote( const Note& ) ) );
	connect( m_pattern->instrumentTrack(), SIGNAL( midiNoteOff( const Note& ) ), this, SLOT( finishRecordNote( const Note& ) ) );
	connect( m_pattern->instrumentTrack()->pianoModel(), SIGNAL( dataChanged() ), this, SLOT( update() ) );

	update();
	emit currentPatternChanged();
}



void PianoRoll::hidePattern( Pattern* pattern )
{
	if( m_pattern == pattern )
	{
		setCurrentPattern( NULL );
	}
}

void PianoRoll::selectRegionFromPixels( int xStart, int xEnd )
{

	xStart -= WHITE_KEY_WIDTH;
	xEnd  -= WHITE_KEY_WIDTH;

	// select an area of notes
	int posTicks = xStart * MidiTime::ticksPerBar() / m_ppb +
					m_currentPosition;
	int keyNum = 0;
	m_selectStartTick = posTicks;
	m_selectedTick = 0;
	m_selectStartKey = keyNum;
	m_selectedKeys = 1;
	// change size of selection

	// get tick in which the cursor is posated
	posTicks = xEnd  * MidiTime::ticksPerBar() / m_ppb +
					m_currentPosition;
	keyNum = 120;

	m_selectedTick = posTicks - m_selectStartTick;
	if( (int) m_selectStartTick + m_selectedTick < 0 )
	{
		m_selectedTick = -static_cast<int>(
					m_selectStartTick );
	}
	m_selectedKeys = keyNum - m_selectStartKey;
	if( keyNum <= m_selectStartKey )
	{
		--m_selectedKeys;
	}

	computeSelectedNotes( false );
}






/** \brief qproperty access implementation */

QColor PianoRoll::barLineColor() const
{ return m_barLineColor; }

void PianoRoll::setBarLineColor( const QColor & c )
{ m_barLineColor = c; }

QColor PianoRoll::beatLineColor() const
{ return m_beatLineColor; }

void PianoRoll::setBeatLineColor( const QColor & c )
{ m_beatLineColor = c; }

QColor PianoRoll::lineColor() const
{ return m_lineColor; }

void PianoRoll::setLineColor( const QColor & c )
{ m_lineColor = c; }

QColor PianoRoll::noteModeColor() const
{ return m_noteModeColor; }

void PianoRoll::setNoteModeColor( const QColor & c )
{ m_noteModeColor = c; }

QColor PianoRoll::noteColor() const
{ return m_noteColor; }

void PianoRoll::setNoteColor( const QColor & c )
{ m_noteColor = c; }

QColor PianoRoll::noteTextColor() const
{ return m_noteTextColor; }

void PianoRoll::setNoteTextColor( const QColor & c )
{ m_noteTextColor = c; }

QColor PianoRoll::barColor() const
{ return m_barColor; }

void PianoRoll::setBarColor( const QColor & c )
{ m_barColor = c; }

QColor PianoRoll::selectedNoteColor() const
{ return m_selectedNoteColor; }

void PianoRoll::setSelectedNoteColor( const QColor & c )
{ m_selectedNoteColor = c; }

QColor PianoRoll::textColor() const
{ return m_textColor; }

void PianoRoll::setTextColor( const QColor & c )
{ m_textColor = c; }

QColor PianoRoll::textColorLight() const
{ return m_textColorLight; }

void PianoRoll::setTextColorLight( const QColor & c )
{ m_textColorLight = c; }

QColor PianoRoll::textShadow() const
{ return m_textShadow; }

void PianoRoll::setTextShadow( const QColor & c )
{ m_textShadow = c; }

QColor PianoRoll::markedSemitoneColor() const
{ return m_markedSemitoneColor; }

void PianoRoll::setMarkedSemitoneColor( const QColor & c )
{ m_markedSemitoneColor = c; }

int PianoRoll::noteOpacity() const
{ return m_noteOpacity; }

void PianoRoll::setNoteOpacity( const int i )
{ m_noteOpacity = i; }

bool PianoRoll::noteBorders() const
{ return m_noteBorders; }

void PianoRoll::setNoteBorders( const bool b )
{ m_noteBorders = b; }

QColor PianoRoll::ghostNoteColor() const
{ return m_ghostNoteColor; }

void PianoRoll::setGhostNoteColor( const QColor & c )
{ m_ghostNoteColor = c; }

QColor PianoRoll::ghostNoteTextColor() const
{ return m_ghostNoteTextColor; }

void PianoRoll::setGhostNoteTextColor( const QColor & c )
{ m_ghostNoteTextColor = c; }

int PianoRoll::ghostNoteOpacity() const
{ return m_ghostNoteOpacity; }

void PianoRoll::setGhostNoteOpacity( const int i )
{ m_ghostNoteOpacity = i; }

bool PianoRoll::ghostNoteBorders() const
{ return m_ghostNoteBorders; }

void PianoRoll::setGhostNoteBorders( const bool b )
{ m_ghostNoteBorders = b; }

QColor PianoRoll::backgroundShade() const
{ return m_backgroundShade; }

void PianoRoll::setBackgroundShade( const QColor & c )
{ m_backgroundShade = c; }




void PianoRoll::drawNoteRect( QPainter & p, int x, int y,
				int width, const Note * n, const QColor & noteCol, const QColor & noteTextColor,
				const QColor & selCol, const int noteOpc, const bool borders, bool drawNoteName )
{
	++x;
	++y;
	width -= 2;

	if( width <= 0 )
	{
		width = 2;
	}

	// Volume
	float const volumeRange = static_cast<float>(MaxVolume - MinVolume);
	float const volumeSpan = static_cast<float>(n->getVolume() - MinVolume);
	float const volumeRatio = volumeSpan / volumeRange;
	int volVal = qMin( 255, 100 + static_cast<int>( volumeRatio * 155.0f) );

	// Panning
	float const panningRange = static_cast<float>(PanningRight - PanningLeft);
	float const leftPanSpan = static_cast<float>(PanningRight - n->getPanning());
	float const rightPanSpan = static_cast<float>(n->getPanning() - PanningLeft);

	float leftPercent = qMin<float>( 1.0f, leftPanSpan / panningRange * 2.0f );
	float rightPercent = qMin<float>( 1.0f, rightPanSpan / panningRange * 2.0f );

	QColor col = QColor( noteCol );
	QPen pen;

	if( n->selected() )
	{
		col = QColor( selCol );
	}

	const int borderWidth = borders ? 1 : 0;

	const int noteHeight = m_keyLineHeight - 1 - borderWidth;
	int noteWidth = width + 1 - borderWidth;

	// adjust note to make it a bit faded if it has a lower volume
	// in stereo using gradients
	QColor lcol = QColor::fromHsv( col.hue(), col.saturation(),
				       static_cast<int>(volVal * leftPercent), noteOpc );
	QColor rcol = QColor::fromHsv( col.hue(), col.saturation(),
				       static_cast<int>(volVal * rightPercent), noteOpc );

	QLinearGradient gradient( x, y, x, y + noteHeight );
	gradient.setColorAt( 0, rcol );
	gradient.setColorAt( 1, lcol );
	p.setBrush( gradient );

	if ( borders )
	{
		p.setPen( col );
	}
	else
	{
		p.setPen( Qt::NoPen );
	}

	p.drawRect( x, y, noteWidth, noteHeight );

	// Draw note key text
	if (drawNoteName)
	{
		p.save();
		int const noteTextHeight = static_cast<int>(noteHeight * 0.8);
		if (noteTextHeight > 6)
		{
			QString noteKeyString = getNoteString(n->key());

			QFont noteFont(p.font());
			noteFont.setPixelSize(noteTextHeight);
			QFontMetrics fontMetrics(noteFont);
			QSize textSize = fontMetrics.size(Qt::TextSingleLine, noteKeyString);

			int const distanceToBorder = 2;
			int const xOffset = borderWidth + distanceToBorder;

			// noteTextHeight, textSize are not suitable for determining vertical spacing,
			// capHeight() can be used for this, but requires Qt 5.8.
			// We use boundingRect() with QChar (the QString version returns wrong value).
			QRect const boundingRect = fontMetrics.boundingRect(QChar::fromLatin1('H'));
			int const yOffset = (noteHeight - boundingRect.top() - boundingRect.bottom()) / 2;

			if (textSize.width() < noteWidth - xOffset)
			{
				p.setPen(noteTextColor);
				p.setFont(noteFont);
				QPoint textStart(x + xOffset, y + yOffset);

				p.drawText(textStart, noteKeyString);
			}
		}
		p.restore();
	}

	// draw the note endmark, to hint the user to resize
	p.setBrush( col );
	if( width > 2 )
	{
		const int endmarkWidth = 3 - borderWidth;
		p.drawRect( x + noteWidth - endmarkWidth, y, endmarkWidth, noteHeight );
	}
}




void PianoRoll::drawDetuningInfo( QPainter & _p, const Note * _n, int _x,
								int _y ) const
{
	int middle_y = _y + m_keyLineHeight / 2;
	_p.setPen( noteColor() );
	_p.setClipRect(WHITE_KEY_WIDTH, PR_TOP_MARGIN,
		width() - WHITE_KEY_WIDTH,
		keyAreaBottom() - PR_TOP_MARGIN);

	int old_x = 0;
	int old_y = 0;

	timeMap & map = _n->detuning()->automationPattern()->getTimeMap();
	for( timeMap::ConstIterator it = map.begin(); it != map.end(); ++it )
	{
		int pos_ticks = it.key();
		int pos_x = _x + pos_ticks * m_ppb / MidiTime::ticksPerBar();

		const float level = it.value();

		int pos_y = middle_y - level * m_keyLineHeight;

		if( old_x != 0 && old_y != 0 )
		{
			switch( _n->detuning()->automationPattern()->progressionType() )
			{
			case AutomationPattern::DiscreteProgression:
				_p.drawLine( old_x, old_y, pos_x, old_y );
				_p.drawLine( pos_x, old_y, pos_x, pos_y );
				break;
			case AutomationPattern::CubicHermiteProgression: /* TODO */
			case AutomationPattern::LinearProgression:
				_p.drawLine( old_x, old_y, pos_x, pos_y );
				break;
			}
		}

		_p.drawLine( pos_x - 1, pos_y, pos_x + 1, pos_y );
		_p.drawLine( pos_x, pos_y - 1, pos_x, pos_y + 1 );

		old_x = pos_x;
		old_y = pos_y;
	}
}




void PianoRoll::removeSelection()
{
	m_selectStartTick = 0;
	m_selectedTick = 0;
	m_selectStartKey = 0;
	m_selectedKeys = 0;
}




void PianoRoll::clearSelectedNotes()
{
	if( m_pattern != NULL )
	{
		for( Note *note : m_pattern->notes() )
		{
			note->setSelected( false );
		}
	}
}



void PianoRoll::shiftSemiTone(int amount) //Shift notes by amount semitones
{
	if (!hasValidPattern()) { return; }

	auto selectedNotes = getSelectedNotes();
	//If no notes are selected, shift all of them, otherwise shift selection
	if (selectedNotes.empty()) { return shiftSemiTone(m_pattern->notes(), amount); }
	else { return shiftSemiTone(selectedNotes, amount); }
}

void PianoRoll::shiftSemiTone(NoteVector notes, int amount)
{
	m_pattern->addJournalCheckPoint();
	for (Note *note : notes) { note->setKey( note->key() + amount ); }

	m_pattern->rearrangeAllNotes();
	m_pattern->dataChanged();
	//We modified the song
	update();
	gui->songEditor()->update();
}




void PianoRoll::shiftPos(int amount) //Shift notes pos by amount
{
	if (!hasValidPattern()) { return; }

	auto selectedNotes = getSelectedNotes();
	//If no notes are selected, shift all of them, otherwise shift selection
	if (selectedNotes.empty()) { return shiftPos(m_pattern->notes(), amount); }
	else { return shiftPos(selectedNotes, amount); }
}

void PianoRoll::shiftPos(NoteVector notes, int amount)
{
	m_pattern->addJournalCheckPoint();
	auto leftMostPos = notes.first()->pos();
	//Limit leftwards shifts to prevent moving left of pattern start
	auto shiftAmount = (leftMostPos > -amount) ? amount : -leftMostPos;
	if (shiftAmount == 0) { return; }

	for (Note *note : notes) { note->setPos( note->pos() + shiftAmount ); }

	m_pattern->rearrangeAllNotes();
	m_pattern->updateLength();
	m_pattern->dataChanged();
	// we modified the song
	update();
	gui->songEditor()->update();
}




bool PianoRoll::isSelection() const // are any notes selected?
{
	for( const Note *note : m_pattern->notes() )
	{
		if( note->selected() )
		{
			return true;
		}
	}

	return false;
}



int PianoRoll::selectionCount() const // how many notes are selected?
{
	return getSelectedNotes().size();
}



void PianoRoll::keyPressEvent(QKeyEvent* ke)
{
	if(m_stepRecorder.isRecording())
	{
		bool handled = m_stepRecorder.keyPressEvent(ke);
		if(handled)
		{
			ke->accept();
			update();
			return;
		}
	}

	if( hasValidPattern() && ke->modifiers() == Qt::NoModifier )
	{
		const int key_num = PianoView::getKeyFromKeyEvent( ke ) + ( DefaultOctave - 1 ) * KeysPerOctave;

		if (!ke->isAutoRepeat() && key_num > -1)
		{
			m_pattern->instrumentTrack()->pianoModel()->handleKeyPress(key_num);
			//  if a chord is set, play all chord notes (simulate click on all):
			playChordNotes(key_num);
			ke->accept();
		}
	}

	switch( ke->key() )
	{
		case Qt::Key_Up:
		case Qt::Key_Down:
			{
				int direction = (ke->key() == Qt::Key_Up ? +1 : -1);
				if( ( ke->modifiers() & Qt::ControlModifier ) && m_action == ActionNone )
				{
					// shift selection up an octave
					// if nothing selected, shift _everything_
					if (hasValidPattern())
					{
						shiftSemiTone( 12 * direction );
					}
				}
				else if((ke->modifiers() & Qt::ShiftModifier) && m_action == ActionNone)
				{
					// Move selected notes up by one semitone
					if (hasValidPattern())
					{
						shiftSemiTone( 1 * direction );
					}
				}
				else
				{
					// scroll
					m_topBottomScroll->setValue( m_topBottomScroll->value() -
						cm_scrollAmtVert * direction );

					// if they are moving notes around or resizing,
					// recalculate the note/resize position
					if( m_action == ActionMoveNote ||
							m_action == ActionResizeNote )
					{
						dragNotes( m_lastMouseX, m_lastMouseY,
									ke->modifiers() & Qt::AltModifier,
									ke->modifiers() & Qt::ShiftModifier,
									ke->modifiers() & Qt::ControlModifier );
					}
				}
				ke->accept();
				break;
			}

		case Qt::Key_Right:
		case Qt::Key_Left:
			{
				int direction = (ke->key() == Qt::Key_Right ? +1 : -1);
				if( ke->modifiers() & Qt::ControlModifier && m_action == ActionNone )
				{
					// Move selected notes by one bar to the left
					if (hasValidPattern())
					{
						shiftPos( direction * MidiTime::ticksPerBar() );
					}
				}
				else if( ke->modifiers() & Qt::ShiftModifier && m_action == ActionNone)
				{
					// move notes
					if (hasValidPattern())
					{
						bool quantized = ! ( ke->modifiers() & Qt::AltModifier );
						int amt = quantized ? quantization() : 1;
						shiftPos( direction * amt );
					}
				}
				else if( ke->modifiers() & Qt::AltModifier)
				{
					// switch to editing a pattern adjacent to this one in the song editor
					if (hasValidPattern())
					{
						Pattern * p = direction > 0 ? m_pattern->nextPattern()
										: m_pattern->previousPattern();
						if(p != NULL)
						{
							setCurrentPattern(p);
						}
					}
				}
				else
				{
					// scroll
					m_leftRightScroll->setValue( m_leftRightScroll->value() +
						direction * cm_scrollAmtHoriz );

					// if they are moving notes around or resizing,
					// recalculate the note/resize position
					if( m_action == ActionMoveNote ||
							m_action == ActionResizeNote )
					{
						dragNotes( m_lastMouseX, m_lastMouseY,
									ke->modifiers() & Qt::AltModifier,
									ke->modifiers() & Qt::ShiftModifier,
									ke->modifiers() & Qt::ControlModifier );
					}

				}
				ke->accept();
				break;
			}

		case Qt::Key_A:
			if( ke->modifiers() & Qt::ControlModifier )
			{
				ke->accept();
				if (ke->modifiers() & Qt::ShiftModifier)
				{
					// Ctrl + Shift + A = deselect all notes
					clearSelectedNotes();
				}
				else
				{
					// Ctrl + A = select all notes
					selectAll();
				}
				update();
			}
			break;

		case Qt::Key_Escape:
			// Same as Ctrl + Shift + A
			clearSelectedNotes();
			break;

		case Qt::Key_Backspace:
		case Qt::Key_Delete:
			deleteSelectedNotes();
			ke->accept();
			break;

		case Qt::Key_Home:
			m_timeLine->pos().setTicks( 0 );
			m_timeLine->updatePosition();
			ke->accept();
			break;

		case Qt::Key_0:
		case Qt::Key_1:
		case Qt::Key_2:
		case Qt::Key_3:
		case Qt::Key_4:
		case Qt::Key_5:
		case Qt::Key_6:
		case Qt::Key_7:
		case Qt::Key_8:
		case Qt::Key_9:
		{
			int len = 1 + ke->key() - Qt::Key_0;
			if( len == 10 )
			{
				len = 0;
			}
			if( ke->modifiers() & ( Qt::ControlModifier | Qt::KeypadModifier ) )
			{
				m_noteLenModel.setValue( len );
				ke->accept();
			}
			else if( ke->modifiers() & Qt::AltModifier )
			{
				m_quantizeModel.setValue( len );
				ke->accept();
			}
			break;
		}

		case Qt::Key_Control:
			// Enter selection mode if:
			// -> this window is active
			// -> shift is not pressed
			// (<S-C-drag> is shortcut for sticky note resize)
			if ( !( ke->modifiers() & Qt::ShiftModifier ) && isActiveWindow() )
			{
				m_ctrlMode = m_editMode;
				m_editMode = ModeSelect;
				setCursor( Qt::ArrowCursor );
				ke->accept();
			}
			break;
		default:
			break;
	}

	update();
}




void PianoRoll::keyReleaseEvent(QKeyEvent* ke )
{
	if( hasValidPattern() && ke->modifiers() == Qt::NoModifier )
	{
		const int key_num = PianoView::getKeyFromKeyEvent( ke ) + ( DefaultOctave - 1 ) * KeysPerOctave;
		if (!ke->isAutoRepeat() && key_num > -1)
		{
			m_pattern->instrumentTrack()->pianoModel()->handleKeyRelease(key_num);
			// if a chord is set, simulate click release on all chord notes
			pauseChordNotes(key_num);
			ke->accept();
		}
	}

	switch( ke->key() )
	{
		case Qt::Key_Control:
			computeSelectedNotes( ke->modifiers() & Qt::ShiftModifier);
			m_editMode = m_ctrlMode;
			update();
			break;

		// update after undo/redo
		case Qt::Key_Z:
		case Qt::Key_R:
			if( hasValidPattern() && ke->modifiers() == Qt::ControlModifier )
			{
				update();
			}
			break;
	}

	update();
}




void PianoRoll::leaveEvent(QEvent * e )
{
	QWidget::leaveEvent( e );
	s_textFloat->hide();
	update(); // cleaning inner mouse-related graphics
}




int PianoRoll::noteEditTop() const
{
	return height() - PR_BOTTOM_MARGIN -
		m_notesEditHeight + NOTE_EDIT_RESIZE_BAR;
}




int PianoRoll::noteEditBottom() const
{
	return height() - PR_BOTTOM_MARGIN;
}




int PianoRoll::noteEditRight() const
{
	return width() - PR_RIGHT_MARGIN;
}




int PianoRoll::noteEditLeft() const
{
	return WHITE_KEY_WIDTH;
}




int PianoRoll::keyAreaTop() const
{
	return PR_TOP_MARGIN;
}




int PianoRoll::keyAreaBottom() const
{
	return height() - PR_BOTTOM_MARGIN - m_notesEditHeight;
}




void PianoRoll::mousePressEvent(QMouseEvent * me )
{
	m_startedWithShift = me->modifiers() & Qt::ShiftModifier;

	if( ! hasValidPattern() )
	{
		return;
	}

	if( m_editMode == ModeEditDetuning && noteUnderMouse() )
	{
		static QPointer<AutomationPattern> detuningPattern = nullptr;
		if (detuningPattern.data() != nullptr)
		{
			detuningPattern->disconnect(this);
		}
		Note* n = noteUnderMouse();
		if (n->detuning() == nullptr)
		{
			n->createDetuning();
		}
		detuningPattern = n->detuning()->automationPattern();
		connect(detuningPattern.data(), SIGNAL(dataChanged()), this, SLOT(update()));
		gui->automationEditor()->open(detuningPattern);
		return;
	}

	// if holding control, go to selection mode unless shift is also pressed
	if( me->modifiers() & Qt::ControlModifier && m_editMode != ModeSelect )
	{
		m_ctrlMode = m_editMode;
		m_editMode = ModeSelect;
		setCursor( Qt::ArrowCursor );
		update();
	}

	// keep track of the point where the user clicked down
	if( me->button() == Qt::LeftButton )
	{
		m_moveStartX = me->x();
		m_moveStartY = me->y();
	}

	if( me->y() > keyAreaBottom() && me->y() < noteEditTop() )
	{
		// resizing the note edit area
		m_action = ActionResizeNoteEditArea;
		m_oldNotesEditHeight = m_notesEditHeight;
		return;
	}

	if( me->y() > PR_TOP_MARGIN )
	{
		bool edit_note = ( me->y() > noteEditTop() );

		int key_num = getKey( me->y() );

		int x = me->x();


		if( x > WHITE_KEY_WIDTH )
		{
			// set, move or resize note

			x -= WHITE_KEY_WIDTH;

			// get tick in which the user clicked
			int pos_ticks = x * MidiTime::ticksPerBar() / m_ppb +
							m_currentPosition;


			// get note-vector of current pattern
			const NoteVector & notes = m_pattern->notes();

			// will be our iterator in the following loop
			NoteVector::ConstIterator it = notes.begin()+notes.size()-1;

			// loop through whole note-vector...
			for( int i = 0; i < notes.size(); ++i )
			{
				Note *note = *it;
				MidiTime len = note->length();
				if( len < 0 )
				{
					len = 4;
				}
				// and check whether the user clicked on an
				// existing note or an edit-line
				if( pos_ticks >= note->pos() &&
						len > 0 &&
					(
					( ! edit_note &&
					pos_ticks <= note->pos() + len &&
					note->key() == key_num )
					||
					( edit_note &&
					pos_ticks <= note->pos() +
						NOTE_EDIT_LINE_WIDTH * MidiTime::ticksPerBar() / m_ppb )
					)
					)
				{
					break;
				}
				--it;
			}

			// first check whether the user clicked in note-edit-
			// area
			if( edit_note )
			{
				m_pattern->addJournalCheckPoint();
				// scribble note edit changes
				mouseMoveEvent( me );
				return;
			}
			// left button??
			else if( me->button() == Qt::LeftButton &&
							m_editMode == ModeDraw )
			{
				// whether this action creates new note(s) or not
				bool is_new_note = false;

				Note * created_new_note = NULL;
				// did it reach end of vector because
				// there's no note??
				if( it == notes.begin()-1 )
				{
					is_new_note = true;
					m_pattern->addJournalCheckPoint();

					// then set new note

					// clear selection and select this new note
					clearSelectedNotes();

					// +32 to quanitize the note correctly when placing notes with
					// the mouse.  We do this here instead of in note.quantized
					// because live notes should still be quantized at the half.
					MidiTime note_pos( pos_ticks - ( quantization() / 2 ) );
					MidiTime note_len( newNoteLen() );

					Note new_note( note_len, note_pos, key_num );
					new_note.setSelected( true );
					new_note.setPanning( m_lastNotePanning );
					new_note.setVolume( m_lastNoteVolume );
					created_new_note = m_pattern->addNote( new_note );

					const InstrumentFunctionNoteStacking::Chord & chord = InstrumentFunctionNoteStacking::ChordTable::getInstance()
						.getChordByName( m_chordModel.currentText() );

					if( ! chord.isEmpty() )
					{
						// if a chord is selected, create following notes in chord
						// or arpeggio mode
						const bool arpeggio = me->modifiers() & Qt::ShiftModifier;
						for( int i = 1; i < chord.size(); i++ )
						{
							if( arpeggio )
							{
								note_pos += note_len;
							}
							Note new_note( note_len, note_pos, key_num + chord[i] );
							new_note.setSelected( true );
							new_note.setPanning( m_lastNotePanning );
							new_note.setVolume( m_lastNoteVolume );
							m_pattern->addNote( new_note );
						}
					}

					// reset it so that it can be used for
					// ops (move, resize) after this
					// code-block
					it = notes.begin();
					while( it != notes.end() && *it != created_new_note )
					{
						++it;
					}
				}

				Note *current_note = *it;
				m_currentNote = current_note;
				m_lastNotePanning = current_note->getPanning();
				m_lastNoteVolume = current_note->getVolume();
				m_lenOfNewNotes = current_note->length();

				// remember which key and tick we started with
				m_mouseDownKey = m_startKey;
				m_mouseDownTick = m_currentPosition;

				//If clicked on an unselected note, remove selection and select that new note
				if (!m_currentNote->selected())
				{
					clearSelectedNotes();
					m_currentNote->setSelected( true );
				}

				auto selectedNotes = getSelectedNotes();

				m_moveBoundaryLeft = selectedNotes.first()->pos().getTicks();
				m_moveBoundaryRight = selectedNotes.first()->endPos();
				m_moveBoundaryBottom = selectedNotes.first()->key();
				m_moveBoundaryTop = m_moveBoundaryBottom;

				//Figure out the bounding box of all the selected notes
				for (Note *note: selectedNotes)
				{
					// remember note starting positions
					note->setOldKey( note->key() );
					note->setOldPos( note->pos() );
					note->setOldLength( note->length() );

					m_moveBoundaryLeft = qMin(note->pos().getTicks(), (tick_t) m_moveBoundaryLeft);
					m_moveBoundaryRight = qMax((int) note->endPos(), m_moveBoundaryRight);
					m_moveBoundaryBottom = qMin(note->key(), m_moveBoundaryBottom);
					m_moveBoundaryTop = qMax(note->key(), m_moveBoundaryTop);
				}

				// clicked at the "tail" of the note?
				if( pos_ticks * m_ppb / MidiTime::ticksPerBar() >
						m_currentNote->endPos() * m_ppb / MidiTime::ticksPerBar() - RESIZE_AREA_WIDTH
					&& m_currentNote->length() > 0 )
				{
					m_pattern->addJournalCheckPoint();
					// then resize the note
					m_action = ActionResizeNote;

					for (Note *note : selectedNotes)
					{
					    if (note->oldLength() <= 0) { note->setOldLength(4); }
					}

					// set resize-cursor
					setCursor( Qt::SizeHorCursor );
				}
				else
				{
					if( ! created_new_note )
					{
						m_pattern->addJournalCheckPoint();
					}

					// otherwise move it
					m_action = ActionMoveNote;

					// set move-cursor
					setCursor( Qt::SizeAllCursor );

					// if they're holding shift, copy all selected notes
					if( ! is_new_note && me->modifiers() & Qt::ShiftModifier )
					{
						for (Note *note: selectedNotes)
						{
							Note *newNote = m_pattern->addNote(*note, false);
							newNote->setSelected(false);
						}

						if (!selectedNotes.empty())
						{
							// added new notes, so must update engine, song, etc
							Engine::getSong()->setModified();
							update();
							gui->songEditor()->update();
						}
					}

					// play the note
					testPlayNote( m_currentNote );
				}

				Engine::getSong()->setModified();
			}
			else if( ( me->buttons() == Qt::RightButton &&
							m_editMode == ModeDraw ) ||
					m_editMode == ModeErase )
			{
				// erase single note
				m_mouseDownRight = true;
				if( it != notes.begin()-1 )
				{
					m_pattern->addJournalCheckPoint();
					m_pattern->removeNote( *it );
					Engine::getSong()->setModified();
				}
			}
			else if( me->button() == Qt::LeftButton &&
							m_editMode == ModeSelect )
			{
				// select an area of notes

				m_selectStartTick = pos_ticks;
				m_selectedTick = 0;
				m_selectStartKey = key_num;
				m_selectedKeys = 1;
				m_action = ActionSelectNotes;

				// call mousemove to fix glitch where selection
				// appears in wrong spot on mousedown
				mouseMoveEvent( me );
			}

			update();
		}
		else if( me->y() < keyAreaBottom() )
		{
			// reference to last key needed for both
			// right click (used for copy all keys on note)
			// and for playing the key when left-clicked
			m_lastKey = key_num;

			// clicked on keyboard on the left
			if( me->buttons() == Qt::RightButton )
			{
				// right click - tone marker contextual menu
				m_pianoKeySelected = getKey( me->y() );
				m_semiToneMarkerMenu->popup( mapToGlobal( QPoint( me->x(), me->y() ) ) );
			}
			else if( me->buttons() == Qt::LeftButton )
			{
				// left click - play the note
				int v = ( (float) x ) / ( (float) WHITE_KEY_WIDTH ) * MidiDefaultVelocity;
				m_pattern->instrumentTrack()->pianoModel()->handleKeyPress(key_num, v);
				// if a chord is set, play the chords notes as well:
				playChordNotes(key_num, v);
			}
		}
		else
		{
			if( me->buttons() == Qt::LeftButton )
			{
				// clicked in the box below the keys to the left of note edit area
				m_noteEditMode = (NoteEditMode)(((int)m_noteEditMode)+1);
				if( m_noteEditMode == NoteEditCount )
				{
					m_noteEditMode = (NoteEditMode) 0;
				}
				repaint();
			}
			else if( me->buttons() == Qt::RightButton )
			{
				// pop menu asking which one they want to edit
				m_noteEditMenu->popup( mapToGlobal( QPoint( me->x(), me->y() ) ) );
			}
		}
	}
}




void PianoRoll::mouseDoubleClickEvent(QMouseEvent * me )
{
	if( ! hasValidPattern() )
	{
		return;
	}

	// if they clicked in the note edit area, enter value for the volume bar
	if( me->x() > noteEditLeft() && me->x() < noteEditRight()
		&& me->y() > noteEditTop() && me->y() < noteEditBottom() )
	{
		// get values for going through notes
		int pixel_range = 4;
		int x = me->x() - WHITE_KEY_WIDTH;
		const int ticks_start = ( x-pixel_range/2 ) *
					MidiTime::ticksPerBar() / m_ppb + m_currentPosition;
		const int ticks_end = ( x+pixel_range/2 ) *
					MidiTime::ticksPerBar() / m_ppb + m_currentPosition;
		const int ticks_middle = x * MidiTime::ticksPerBar() / m_ppb + m_currentPosition;

		// go through notes to figure out which one we want to change
		bool altPressed = me->modifiers() & Qt::AltModifier;
		NoteVector nv;
		for ( Note * i : m_pattern->notes() )
		{
			if( i->withinRange( ticks_start, ticks_end ) || ( i->selected() && !altPressed ) )
			{
				nv += i;
			}
		}
		// make sure we're on a note
		if( nv.size() > 0 )
		{
			const Note * closest = NULL;
			int closest_dist = 9999999;
			// if we caught multiple notes and we're not editing a
			// selection, find the closest...
			if( nv.size() > 1 && !isSelection() )
			{
				for ( const Note * i : nv )
				{
					const int dist = qAbs( i->pos().getTicks() - ticks_middle );
					if( dist < closest_dist ) { closest = i; closest_dist = dist; }
				}
				// ... then remove all notes from the vector that aren't on the same exact time
				NoteVector::Iterator it = nv.begin();
				while( it != nv.end() )
				{
					const Note *note = *it;
					if( note->pos().getTicks() != closest->pos().getTicks() )
					{
						it = nv.erase( it );
					}
					else
					{
						it++;
					}
				}
			}
			enterValue( &nv );
		}
	}
	else
	{
		QWidget::mouseDoubleClickEvent(me);
	}
}




void PianoRoll::testPlayNote( Note * n )
{
	m_lastKey = n->key();

	if( ! n->isPlaying() && ! m_recording && ! m_stepRecorder.isRecording())
	{
		n->setIsPlaying( true );

		const int baseVelocity = m_pattern->instrumentTrack()->midiPort()->baseVelocity();

		m_pattern->instrumentTrack()->pianoModel()->handleKeyPress(n->key(), n->midiVelocity(baseVelocity));

		// if a chord is set, play the chords notes as well:
		playChordNotes(n->key(), n->midiVelocity(baseVelocity));

		MidiEvent event( MidiMetaEvent, -1, n->key(), panningToMidi( n->getPanning() ) );

		event.setMetaEvent( MidiNotePanning );

		m_pattern->instrumentTrack()->processInEvent( event, 0 );
	}
}




void PianoRoll::pauseTestNotes( bool pause )
{
	for (Note *note : m_pattern->notes())
	{
		if( note->isPlaying() )
		{
			if( pause )
			{
				// stop note
				m_pattern->instrumentTrack()->pianoModel()->handleKeyRelease( note->key() );

				// if a chord was set, stop the chords notes as well:
				pauseChordNotes(note->key());
			}
			else
			{
				// start note
				note->setIsPlaying( false );
				testPlayNote( note );
			}
		}
	}
}

void PianoRoll::playChordNotes(int key, int velocity)
{
	// if a chord is set, play the chords notes beside the base note.
	Piano *pianoModel = m_pattern->instrumentTrack()->pianoModel();
	const InstrumentFunctionNoteStacking::Chord & chord =
			InstrumentFunctionNoteStacking::ChordTable::getInstance().getChordByName(
				m_chordModel.currentText());
	if (!chord.isEmpty())
	{
		for (int i = 1; i < chord.size(); ++i)
		{
			pianoModel->handleKeyPress(key + chord[i], velocity);
		}
	}
}

void PianoRoll::pauseChordNotes(int key)
{
	// if a chord was set, stop the chords notes beside the base note.
	Piano *pianoModel = m_pattern->instrumentTrack()->pianoModel();
	const InstrumentFunctionNoteStacking::Chord & chord =
			InstrumentFunctionNoteStacking::ChordTable::getInstance().getChordByName(
				m_chordModel.currentText());
	if (!chord.isEmpty())
	{
		for (int i = 1; i < chord.size(); ++i)
		{
			pianoModel->handleKeyRelease(key + chord[i]);
		}
	}
}




void PianoRoll::testPlayKey( int key, int velocity, int pan )
{
	Piano *pianoModel = m_pattern->instrumentTrack()->pianoModel();
	// turn off old key
	pianoModel->handleKeyRelease( m_lastKey );
	// if a chord was set, stop the chords notes as well
	pauseChordNotes(m_lastKey);

	// remember which one we're playing
	m_lastKey = key;

	// play new key
	pianoModel->handleKeyPress( key, velocity );
	// and if a chord is set, play chord notes:
	playChordNotes(key, velocity);
}




void PianoRoll::computeSelectedNotes(bool shift)
{
	if( m_selectStartTick == 0 &&
		m_selectedTick == 0 &&
		m_selectStartKey == 0 &&
		m_selectedKeys == 0 )
	{
		// don't bother, there's no selection
		return;
	}

	// setup selection-vars
	int sel_pos_start = m_selectStartTick;
	int sel_pos_end = m_selectStartTick+m_selectedTick;
	if( sel_pos_start > sel_pos_end )
	{
		qSwap<int>( sel_pos_start, sel_pos_end );
	}

	int sel_key_start = m_selectStartKey - m_startKey + 1;
	int sel_key_end = sel_key_start + m_selectedKeys;
	if( sel_key_start > sel_key_end )
	{
		qSwap<int>( sel_key_start, sel_key_end );
	}

	//int y_base = noteEditTop() - 1;
	if( hasValidPattern() )
	{
		for( Note *note : m_pattern->notes() )
		{
			// make a new selection unless they're holding shift
			if( ! shift )
			{
				note->setSelected( false );
			}

			int len_ticks = note->length();

			if( len_ticks == 0 )
			{
				continue;
			}
			else if( len_ticks < 0 )
			{
				len_ticks = 4;
			}

			const int key = note->key() - m_startKey + 1;

			int pos_ticks = note->pos();

			// if the selection even barely overlaps the note
			if( key > sel_key_start &&
				key <= sel_key_end &&
				pos_ticks + len_ticks > sel_pos_start &&
				pos_ticks < sel_pos_end )
			{
				// remove from selection when holding shift
				bool selected = shift && note->selected();
				note->setSelected( ! selected);
			}
		}
	}

	removeSelection();
	update();
}




void PianoRoll::mouseReleaseEvent( QMouseEvent * me )
{
	bool mustRepaint = false;

	s_textFloat->hide();

	if( me->button() & Qt::LeftButton )
	{
		mustRepaint = true;

		if( m_action == ActionSelectNotes && m_editMode == ModeSelect )
		{
			// select the notes within the selection rectangle and
			// then destroy the selection rectangle
			computeSelectedNotes(
					me->modifiers() & Qt::ShiftModifier );
		}
		else if( m_action == ActionMoveNote )
		{
			// we moved one or more notes so they have to be
			// moved properly according to new starting-
			// time in the note-array of pattern
			m_pattern->rearrangeAllNotes();

		}

		if( m_action == ActionMoveNote || m_action == ActionResizeNote )
		{
			// if we only moved one note, deselect it so we can
			// edit the notes in the note edit area
			if( selectionCount() == 1 )
			{
				clearSelectedNotes();
			}
		}
	}

	if( me->button() & Qt::RightButton )
	{
		m_mouseDownRight = false;
		mustRepaint = true;
	}

	if( hasValidPattern() )
	{
		// turn off all notes that are playing
		for ( Note *note : m_pattern->notes() )
		{
			if( note->isPlaying() )
			{
				m_pattern->instrumentTrack()->pianoModel()->
						handleKeyRelease( note->key() );
				pauseChordNotes(note->key());
				note->setIsPlaying( false );
			}
		}

		// stop playing keys that we let go of
		m_pattern->instrumentTrack()->pianoModel()->
						handleKeyRelease( m_lastKey );
		pauseChordNotes(m_lastKey);
	}

	m_currentNote = NULL;
	m_action = ActionNone;

	if( m_editMode == ModeDraw )
	{
		setCursor( Qt::ArrowCursor );
	}

	if( mustRepaint )
	{
		repaint();
	}
}




void PianoRoll::mouseMoveEvent( QMouseEvent * me )
{
	if( ! hasValidPattern() )
	{
		update();
		return;
	}

	if( m_action == ActionNone && me->buttons() == 0 )
	{
		if( me->y() > keyAreaBottom() && me->y() < noteEditTop() )
		{
			setCursor( Qt::SizeVerCursor );
			return;
		}
	}
	else if( m_action == ActionResizeNoteEditArea )
	{
		// change m_notesEditHeight and then repaint
		m_notesEditHeight = qBound<int>(
					NOTE_EDIT_MIN_HEIGHT,
					m_oldNotesEditHeight - ( me->y() - m_moveStartY ),
					height() - PR_TOP_MARGIN - NOTE_EDIT_RESIZE_BAR -
									PR_BOTTOM_MARGIN - KEY_AREA_MIN_HEIGHT );

		m_stepRecorderWidget.setBottomMargin(PR_BOTTOM_MARGIN + m_notesEditHeight);
		repaint();
		return;
	}

	if( me->y() > PR_TOP_MARGIN || m_action != ActionNone )
	{
		bool edit_note = ( me->y() > noteEditTop() )
						&& m_action != ActionSelectNotes;


		int key_num = getKey( me->y() );
		int x = me->x();

		// see if they clicked on the keyboard on the left
		if( x < WHITE_KEY_WIDTH && m_action == ActionNone
		    && ! edit_note && key_num != m_lastKey
			&& me->buttons() & Qt::LeftButton )
		{
			// clicked on a key, play the note
			testPlayKey( key_num, ( (float) x ) / ( (float) WHITE_KEY_WIDTH ) * MidiDefaultVelocity, 0 );
			update();
			return;
		}

		x -= WHITE_KEY_WIDTH;

		if( me->buttons() & Qt::LeftButton
			&& m_editMode == ModeDraw
			&& (m_action == ActionMoveNote || m_action == ActionResizeNote ) )
		{
			// handle moving notes and resizing them
			bool replay_note = key_num != m_lastKey
							&& m_action == ActionMoveNote;

			if( replay_note || ( m_action == ActionMoveNote && ( me->modifiers() & Qt::ShiftModifier ) && ! m_startedWithShift ) )
			{
				pauseTestNotes();
			}

			dragNotes( me->x(), me->y(),
				me->modifiers() & Qt::AltModifier,
				me->modifiers() & Qt::ShiftModifier,
				me->modifiers() & Qt::ControlModifier );

			if( replay_note && m_action == ActionMoveNote && ! ( ( me->modifiers() & Qt::ShiftModifier ) && ! m_startedWithShift ) )
			{
				pauseTestNotes( false );
			}
		}
		else if( m_editMode != ModeErase &&
			( edit_note || m_action == ActionChangeNoteProperty ) &&
				( me->buttons() & Qt::LeftButton || me->buttons() & Qt::MiddleButton
				|| ( me->buttons() & Qt::RightButton && me->modifiers() & Qt::ShiftModifier ) ) )
		{
			// editing note properties

			// Change notes within a certain pixel range of where
			// the mouse cursor is
			int pixel_range = 14;

			// convert to ticks so that we can check which notes
			// are in the range
			int ticks_start = ( x-pixel_range/2 ) *
					MidiTime::ticksPerBar() / m_ppb + m_currentPosition;
			int ticks_end = ( x+pixel_range/2 ) *
					MidiTime::ticksPerBar() / m_ppb + m_currentPosition;

			// get note-vector of current pattern
			const NoteVector & notes = m_pattern->notes();

			// determine what volume/panning to set note to
			// if middle-click, set to defaults
			volume_t vol = DefaultVolume;
			panning_t pan = DefaultPanning;

			if( me->buttons() & Qt::LeftButton )
			{
				vol = qBound<int>( MinVolume,
								MinVolume +
								( ( (float)noteEditBottom() ) - ( (float)me->y() ) ) /
								( (float)( noteEditBottom() - noteEditTop() ) ) *
								( MaxVolume - MinVolume ),
											MaxVolume );
				pan = qBound<int>( PanningLeft,
								PanningLeft +
								( (float)( noteEditBottom() - me->y() ) ) /
								( (float)( noteEditBottom() - noteEditTop() ) ) *
								( (float)( PanningRight - PanningLeft ) ),
										  PanningRight);
			}

			if( m_noteEditMode == NoteEditVolume )
			{
				m_lastNoteVolume = vol;
				showVolTextFloat( vol, me->pos() );
			}
			else if( m_noteEditMode == NoteEditPanning )
			{
				m_lastNotePanning = pan;
				showPanTextFloat( pan, me->pos() );
			}

			// When alt is pressed we only edit the note under the cursor
			bool altPressed = me->modifiers() & Qt::AltModifier;
			// We iterate from last note in pattern to the first,
			// chronologically
			NoteVector::ConstIterator it = notes.begin()+notes.size()-1;
			for( int i = 0; i < notes.size(); ++i )
			{
				Note* n = *it;

				bool isUnderPosition = n->withinRange( ticks_start, ticks_end );
				// Play note under the cursor
				if ( isUnderPosition ) { testPlayNote( n ); }
				// If note is:
				// Under the cursor, when there is no selection
				// Selected, and alt is not pressed
				// Under the cursor, selected, and alt is pressed
				if ( ( isUnderPosition && !isSelection() ) ||
					  ( n->selected() && !altPressed ) ||
					  ( isUnderPosition && n->selected() && altPressed )
					)
				{
					if( m_noteEditMode == NoteEditVolume )
					{
						n->setVolume( vol );

						const int baseVelocity = m_pattern->instrumentTrack()->midiPort()->baseVelocity();

						m_pattern->instrumentTrack()->processInEvent( MidiEvent( MidiKeyPressure, -1, n->key(), n->midiVelocity( baseVelocity ) ) );
					}
					else if( m_noteEditMode == NoteEditPanning )
					{
						n->setPanning( pan );
						MidiEvent evt( MidiMetaEvent, -1, n->key(), panningToMidi( pan ) );
						evt.setMetaEvent( MidiNotePanning );
						m_pattern->instrumentTrack()->processInEvent( evt );
					}
				}
				else if( n->isPlaying() && !isSelection() )
				{
					// mouse not over this note, stop playing it.
					m_pattern->instrumentTrack()->pianoModel()->handleKeyRelease( n->key() );
					pauseChordNotes(n->key());

					n->setIsPlaying( false );
				}


				--it;
			}

			// Emit pattern has changed
			m_pattern->dataChanged();
		}

		else if( me->buttons() == Qt::NoButton && m_editMode == ModeDraw )
		{
			// set move- or resize-cursor

			// get tick in which the cursor is posated
			int pos_ticks = ( x * MidiTime::ticksPerBar() ) /
						m_ppb + m_currentPosition;

			// get note-vector of current pattern
			const NoteVector & notes = m_pattern->notes();

			// will be our iterator in the following loop
			NoteVector::ConstIterator it = notes.begin()+notes.size()-1;

			// loop through whole note-vector...
			for( int i = 0; i < notes.size(); ++i )
			{
				Note *note = *it;
				// and check whether the cursor is over an
				// existing note
				if( pos_ticks >= note->pos() &&
			    		pos_ticks <= note->pos() +
							note->length() &&
					note->key() == key_num &&
					note->length() > 0 )
				{
					break;
				}
				--it;
			}

			// did it reach end of vector because there's
			// no note??
			if( it != notes.begin()-1 )
			{
				Note *note = *it;
				// x coordinate of the right edge of the note
				int noteRightX = ( note->pos() + note->length() -
					m_currentPosition) * m_ppb/MidiTime::ticksPerBar();
				// cursor at the "tail" of the note?
				bool atTail = note->length() > 0 && x > noteRightX -
							RESIZE_AREA_WIDTH;
				Qt::CursorShape cursorShape = atTail ? Qt::SizeHorCursor :
													Qt::SizeAllCursor;
				setCursor( cursorShape );
			}
			else
			{
				// the cursor is over no note, so restore cursor
				setCursor( Qt::ArrowCursor );
			}
		}
		else if( me->buttons() & Qt::LeftButton &&
						m_editMode == ModeSelect &&
						m_action == ActionSelectNotes )
		{
			// change size of selection

			// get tick in which the cursor is posated
			int pos_ticks = x * MidiTime::ticksPerBar() / m_ppb +
							m_currentPosition;

			m_selectedTick = pos_ticks - m_selectStartTick;
			if( (int) m_selectStartTick + m_selectedTick < 0 )
			{
				m_selectedTick = -static_cast<int>(
							m_selectStartTick );
			}
			m_selectedKeys = key_num - m_selectStartKey;
			if( key_num <= m_selectStartKey )
			{
				--m_selectedKeys;
			}
		}
		else if( ( m_editMode == ModeDraw && me->buttons() & Qt::RightButton )
				|| ( m_editMode == ModeErase && me->buttons() ) )
		{
			// holding down right-click to delete notes or holding down
			// any key if in erase mode

			// get tick in which the user clicked
			int pos_ticks = x * MidiTime::ticksPerBar() / m_ppb +
							m_currentPosition;


			// get note-vector of current pattern
			const NoteVector & notes = m_pattern->notes();

			// will be our iterator in the following loop
			NoteVector::ConstIterator it = notes.begin();

			// loop through whole note-vector...
			while( it != notes.end() )
			{
				Note *note = *it;
				MidiTime len = note->length();
				if( len < 0 )
				{
					len = 4;
				}
				// and check whether the user clicked on an
				// existing note or an edit-line
				if( pos_ticks >= note->pos() &&
						len > 0 &&
					(
					( ! edit_note &&
					pos_ticks <= note->pos() + len &&
					note->key() == key_num )
					||
					( edit_note &&
					pos_ticks <= note->pos() +
							NOTE_EDIT_LINE_WIDTH *
						MidiTime::ticksPerBar() /
								m_ppb )
					)
					)
				{
					// delete this note
					m_pattern->removeNote( note );
					Engine::getSong()->setModified();
				}
				else
				{
					++it;
				}
			}
		}
		else if (me->buttons() == Qt::NoButton && m_editMode != ModeDraw)
		{
			// Is needed to restore cursor when it previously was set to
			// Qt::SizeVerCursor (between keyAreaBottom and noteEditTop)
			setCursor( Qt::ArrowCursor );
		}
	}
	else
	{
		if( me->buttons() & Qt::LeftButton &&
					m_editMode == ModeSelect &&
					m_action == ActionSelectNotes )
		{

			int x = me->x() - WHITE_KEY_WIDTH;
			if( x < 0 && m_currentPosition > 0 )
			{
				x = 0;
				QCursor::setPos( mapToGlobal( QPoint(
							WHITE_KEY_WIDTH,
							me->y() ) ) );
				if( m_currentPosition >= 4 )
				{
					m_leftRightScroll->setValue(
							m_currentPosition - 4 );
				}
				else
				{
					m_leftRightScroll->setValue( 0 );
				}
			}
			else if( x > width() - WHITE_KEY_WIDTH )
			{
				x = width() - WHITE_KEY_WIDTH;
				QCursor::setPos( mapToGlobal( QPoint( width(),
							me->y() ) ) );
				m_leftRightScroll->setValue( m_currentPosition +
									4 );
			}

			// get tick in which the cursor is posated
			int pos_ticks = x * MidiTime::ticksPerBar()/ m_ppb +
							m_currentPosition;

			m_selectedTick = pos_ticks -
							m_selectStartTick;
			if( (int) m_selectStartTick + m_selectedTick <
									0 )
			{
				m_selectedTick = -static_cast<int>(
							m_selectStartTick );
			}


			int key_num = getKey( me->y() );
			int visible_keys = ( height() - PR_TOP_MARGIN -
						PR_BOTTOM_MARGIN -
						m_notesEditHeight ) /
							m_keyLineHeight + 2;
			const int s_key = m_startKey - 1;

			if( key_num <= s_key )
			{
				QCursor::setPos( mapToGlobal( QPoint( me->x(),
							keyAreaBottom() ) ) );
				m_topBottomScroll->setValue(
					m_topBottomScroll->value() + 1 );
				key_num = s_key;
			}
			else if( key_num >= s_key + visible_keys )
			{
				QCursor::setPos( mapToGlobal( QPoint( me->x(),
							PR_TOP_MARGIN ) ) );
				m_topBottomScroll->setValue(
					m_topBottomScroll->value() - 1 );
				key_num = s_key + visible_keys;
			}

			m_selectedKeys = key_num - m_selectStartKey;
			if( key_num <= m_selectStartKey )
			{
				--m_selectedKeys;
			}
		}
		setCursor( Qt::ArrowCursor );
	}

	m_lastMouseX = me->x();
	m_lastMouseY = me->y();

	update();
}




void PianoRoll::dragNotes( int x, int y, bool alt, bool shift, bool ctrl )
{
	// dragging one or more notes around

	// convert pixels to ticks and keys
	int off_x = x - m_moveStartX;
	int off_ticks = off_x * MidiTime::ticksPerBar() / m_ppb;
	int off_key = getKey( y ) - getKey( m_moveStartY );

	// handle scroll changes while dragging
	off_ticks -= m_mouseDownTick - m_currentPosition;
	off_key -= m_mouseDownKey - m_startKey;


	// if they're not holding alt, quantize the offset
	if( ! alt )
	{
		off_ticks = floor( off_ticks / quantization() )
						* quantization();
	}

	// make sure notes won't go outside boundary conditions
	if( m_action == ActionMoveNote && ! ( shift && ! m_startedWithShift ) )
	{
		if( m_moveBoundaryLeft + off_ticks < 0 )
		{
			off_ticks -= (off_ticks + m_moveBoundaryLeft);
		}
		if( m_moveBoundaryTop + off_key > NumKeys )
		{
			off_key -= NumKeys - (m_moveBoundaryTop + off_key);
		}
		if( m_moveBoundaryBottom + off_key < 0 )
		{
			off_key -= (m_moveBoundaryBottom + off_key);
		}
	}


	// get note-vector of current pattern
	const NoteVector & notes = m_pattern->notes();

	if (m_action == ActionMoveNote)
	{
		for (Note *note : getSelectedNotes())
		{
			if (shift && ! m_startedWithShift)
			{
				// quick resize, toggled by holding shift after starting a note move, but not before
				int ticks_new = note->oldLength().getTicks() + off_ticks;
				if( ticks_new <= 0 )
				{
					ticks_new = 1;
				}
				note->setLength( MidiTime( ticks_new ) );
				m_lenOfNewNotes = note->length();
			}
			else
			{
				// moving note
				int pos_ticks = note->oldPos().getTicks() + off_ticks;
				int key_num = note->oldKey() + off_key;

				// ticks can't be negative
				pos_ticks = qMax(0, pos_ticks);
				// upper/lower bound checks on key_num
				key_num = qMax(0, key_num);
				key_num = qMin(key_num, NumKeys);

				note->setPos( MidiTime( pos_ticks ) );
				note->setKey( key_num );
			}
		}
	}
	else if (m_action == ActionResizeNote)
	{
		// When resizing notes:
		// If shift is not pressed, resize the selected notes but do not rearrange them
		// If shift is pressed we resize and rearrange only the selected notes
		// If shift + ctrl then we also rearrange all posterior notes (sticky)
		// If shift is pressed but only one note is selected, apply sticky
		
		auto selectedNotes = getSelectedNotes();

		if (shift)
		{
			// Algorithm:
			// Relative to the starting point of the left-most selected note,
			//   all selected note start-points and *endpoints* (not length) should be scaled by a calculated factor.
			// This factor is such that the endpoint of the note whose handle is being dragged should lie under the cursor.
			// first, determine the start-point of the left-most selected note:
			int stretchStartTick = -1;
			for (const Note *note : selectedNotes)
			{
				if (stretchStartTick < 0 || note->oldPos().getTicks() < stretchStartTick)
				{
					stretchStartTick = note->oldPos().getTicks();
				}
			}
			// determine the ending tick of the right-most selected note
			const Note *posteriorNote = nullptr;
			for (const Note *note : selectedNotes)
			{
				if (posteriorNote == nullptr ||
					note->oldPos().getTicks() + note->oldLength().getTicks() >
					posteriorNote->oldPos().getTicks() + posteriorNote->oldLength().getTicks())
				{
					posteriorNote = note;
				}
			}
			int posteriorEndTick = posteriorNote->pos().getTicks() + posteriorNote->length().getTicks();
			// end-point of the note whose handle is being dragged:
			int stretchEndTick = m_currentNote->oldPos().getTicks() + m_currentNote->oldLength().getTicks();
			// Calculate factor by which to scale the start-point and end-point of all selected notes
			float scaleFactor = (float)(stretchEndTick - stretchStartTick + off_ticks) / qMax(1, stretchEndTick - stretchStartTick);
			scaleFactor = qMax(0.0f, scaleFactor);

			// process all selected notes & determine how much the endpoint of the right-most note was shifted
			int posteriorDeltaThisFrame = 0;
			for (Note *note : selectedNotes)
			{
				// scale relative start and end positions by scaleFactor
				int newStart = stretchStartTick + scaleFactor *
					(note->oldPos().getTicks() - stretchStartTick);
				int newEnd = stretchStartTick + scaleFactor *
					(note->oldPos().getTicks()+note->oldLength().getTicks() - stretchStartTick);
				// if  not holding alt, quantize the offsets
				if (!alt)
				{
					// quantize start time
					int oldStart = note->oldPos().getTicks();
					int startDiff = newStart - oldStart;
					startDiff = floor(startDiff / quantization()) * quantization();
					newStart = oldStart + startDiff;
					// quantize end time
					int oldEnd = oldStart + note->oldLength().getTicks();
					int endDiff = newEnd - oldEnd;
					endDiff = floor(endDiff / quantization()) * quantization();
					newEnd = oldEnd + endDiff;
				}
				int newLength = qMax(1, newEnd-newStart);
				if (note == posteriorNote)
				{
					posteriorDeltaThisFrame = (newStart+newLength) -
						(note->pos().getTicks() + note->length().getTicks());
				}
				note->setLength( MidiTime(newLength) );
				note->setPos( MidiTime(newStart) );

				m_lenOfNewNotes = note->length();
			}
			if (ctrl || selectionCount() == 1)
			{
				// if holding ctrl or only one note is selected, reposition posterior notes
				for (Note *note : notes)
				{
					if (!note->selected() && note->pos().getTicks() >= posteriorEndTick)
					{
						int newStart = note->pos().getTicks() + posteriorDeltaThisFrame;
						note->setPos( MidiTime(newStart) );
					}
				}
			}
		}
		else
		{
			// shift is not pressed; stretch length of selected notes but not their position
			for (Note *note : selectedNotes)
			{
				int newLength = note->oldLength() + off_ticks;
				newLength = qMax(1, newLength);
				note->setLength( MidiTime(newLength) );

				m_lenOfNewNotes = note->length();
			}
		}
	}

	m_pattern->updateLength();
	m_pattern->dataChanged();
	Engine::getSong()->setModified();
}

int PianoRoll::xCoordOfTick( int tick )
{
	return WHITE_KEY_WIDTH + ( ( tick - m_currentPosition )
		* m_ppb / MidiTime::ticksPerBar() );
}

void PianoRoll::paintEvent(QPaintEvent * pe )
{
	bool drawNoteNames = ConfigManager::inst()->value( "ui", "printnotelabels").toInt();

	QStyleOption opt;
	opt.initFrom( this );
	QPainter p( this );
	style()->drawPrimitive( QStyle::PE_Widget, &opt, &p, this );

	QBrush bgColor = p.background();

	// fill with bg color
	p.fillRect( 0, 0, width(), height(), bgColor );

	// set font-size to 8
	p.setFont( pointSize<8>( p.font() ) );
	QFontMetrics fontMetrics(p.font());
	QRect const boundingRect = fontMetrics.boundingRect(QChar::fromLatin1('H'));
	// This is two times of the y coordinate of the center of the bounding rectangle
	// (-(top+bottom)=-2(center)) but labelHeight is more intuitive/describing name
	int const labelHeight = - boundingRect.top() - boundingRect.bottom();

	// y_offset is used to align the piano-keys on the key-lines
	int y_offset = 0;

	// calculate y_offset according to first key
	switch( prKeyOrder[m_startKey % KeysPerOctave] )
	{
		case PR_BLACK_KEY: y_offset = m_keyLineHeight / 4; break;
		case PR_WHITE_KEY_BIG: y_offset = m_keyLineHeight / 2; break;
		case PR_WHITE_KEY_SMALL:
			if( prKeyOrder[( ( m_startKey + 1 ) %
					KeysPerOctave)] != PR_BLACK_KEY )
			{
				y_offset = m_keyLineHeight / 2;
			}
			break;
	}
	// start drawing at the bottom
	int key_line_y = qMin(keyAreaBottom() - 1, m_keyLineHeight * NumKeys);
	// we need to set m_notesEditHeight here because it needs to fill in the
	// rest of the window if key_line_y is bound to m_keyLineHeight * NumKeys
	if (key_line_y == m_keyLineHeight * NumKeys) {
		m_notesEditHeight = height() - (PR_TOP_MARGIN + m_keyLineHeight * NumKeys);
	}
	// used for aligning black-keys later
	int first_white_key_height = m_whiteKeySmallHeight;
	// key-counter - only needed for finding out whether the processed
	// key is the first one
	int keys_processed = 0;

	int key = m_startKey;

	// draw all white keys...
	for( int y = key_line_y + 1 + y_offset; y > PR_TOP_MARGIN;
			key_line_y -= m_keyLineHeight, ++keys_processed )
	{
		// check for white key that is only half visible on the
		// bottom of piano-roll
		if( keys_processed == 0 &&
			prKeyOrder[m_startKey % KeysPerOctave] ==
								PR_BLACK_KEY )
		{
			// draw it!
			p.drawPixmap( PIANO_X, y - m_whiteKeySmallHeight, WHITE_KEY_WIDTH, m_whiteKeySmallHeight,
							*s_whiteKeySmallPm );
			// update y-pos
			y -= m_whiteKeySmallHeight / 2;
			// move first black key down (we didn't draw whole
			// white key so black key needs to be lifted down)
			// (default for first_white_key_height =
			// m_whiteKeySmallHeight, so m_whiteKeySmallHeight/2
			// is smaller)
			first_white_key_height = m_whiteKeySmallHeight / 2;
		}
		// check whether to draw a big or a small white key
		if( prKeyOrder[key % KeysPerOctave] == PR_WHITE_KEY_SMALL )
		{
			// draw a small one while checking if it is pressed or not
			if( hasValidPattern() && m_pattern->instrumentTrack()->pianoModel()->isKeyPressed( key ) )
			{
				p.drawPixmap(PIANO_X, y - m_whiteKeySmallHeight, WHITE_KEY_WIDTH, m_whiteKeySmallHeight,
							*s_whiteKeySmallPressedPm);
			}
			else
			{
				p.drawPixmap(PIANO_X, y - m_whiteKeySmallHeight, WHITE_KEY_WIDTH, m_whiteKeySmallHeight,
							*s_whiteKeySmallPm);
			}
			// update y-pos
			y -= m_whiteKeySmallHeight;

		}
		else if( prKeyOrder[key % KeysPerOctave] == PR_WHITE_KEY_BIG )
		{
			// draw a big one while checking if it is pressed or not
			if( hasValidPattern() && m_pattern->instrumentTrack()->pianoModel()->isKeyPressed( key ) )
			{
				p.drawPixmap(PIANO_X, y - m_whiteKeyBigHeight, WHITE_KEY_WIDTH, m_whiteKeyBigHeight,
							*s_whiteKeyBigPressedPm);
			}
			else
			{
				p.drawPixmap(PIANO_X, y-m_whiteKeyBigHeight, WHITE_KEY_WIDTH, m_whiteKeyBigHeight,
							*s_whiteKeyBigPm);
			}
			// if a big white key has been the first key,
			// black keys needs to be lifted up
			if( keys_processed == 0 )
			{
				first_white_key_height = m_whiteKeyBigHeight;
			}
			// update y-pos
			y -= m_whiteKeyBigHeight;
		}

		// Compute the corrections for the note names
		int yCorrectionForNoteLabels = 0;

		int keyCode = key % KeysPerOctave;
		switch (keyCode)
		{
		case 0: // C
		case 5: // F
			yCorrectionForNoteLabels = (m_whiteKeySmallHeight - labelHeight + 1) / -2;
			break;
		case 2: // D
		case 7: // G
		case 9: // A
			yCorrectionForNoteLabels = (m_whiteKeyBigHeight / 2 - labelHeight + 1) / -2;
			break;
		case 4: // E
		case 11: // B
			// calculate center point of key and move half of text
			yCorrectionForNoteLabels = -(((m_whiteKeySmallHeight - (m_whiteKeySmallHeight * 2 + 3) / 6) / 4)
										 - labelHeight / 2);
			break;
		}

		if( Piano::isWhiteKey( key ) )
		{
			// Draw note names if activated in the preferences, C notes are always drawn
			if ( (key % 12 == 0 || drawNoteNames) && m_keyLineHeight > 10 )
			{
				QString noteString = getNoteString( key );

				QPoint textStart( WHITE_KEY_WIDTH - 18, key_line_y );
				textStart += QPoint( 0, yCorrectionForNoteLabels );

				p.setPen( textShadow() );
				p.drawText( textStart + QPoint( 1, 1 ), noteString );
				// The C key is painted darker than the other ones
				if ( key % 12 == 0 )
				{
					p.setPen( textColor() );
				}
				else
				{
					p.setPen( textColorLight() );
				}
				p.drawText( textStart, noteString );
			}
		}
		++key;
	}

	// reset all values, because now we're going to draw all black keys
	key = m_startKey;
	keys_processed = 0;
	int white_cnt = 0;
	key_line_y = qMin(keyAreaBottom(), m_keyLineHeight * NumKeys);

	// and go!
	for( int y = key_line_y + y_offset;
					y > PR_TOP_MARGIN; ++keys_processed )
	{
		// check for black key that is only half visible on the bottom
		// of piano-roll
		if( keys_processed == 0
		    // current key may not be a black one
		    && prKeyOrder[key % KeysPerOctave] != PR_BLACK_KEY
		    // but the previous one must be black (we must check this
		    // because there might be two white keys (E-F)
		    && prKeyOrder[( key - 1 ) % KeysPerOctave] ==
								PR_BLACK_KEY )
		{
			// draw the black key!
			p.drawPixmap( PIANO_X, y - m_blackKeyHeight / 2, BLACK_KEY_WIDTH, m_blackKeyHeight,
								*s_blackKeyPm );
			// is the one after the start-note a black key??
			if( prKeyOrder[( key + 1 ) % KeysPerOctave] !=
								PR_BLACK_KEY )
			{
				// no, then move it up!
				y -= m_keyLineHeight / 2;
			}
		}
		// current key black?
		if( prKeyOrder[key % KeysPerOctave] == PR_BLACK_KEY)
		{
			// then draw it (calculation of y very complicated,
			// but that's the only working solution, sorry...)
			// check if the key is pressed or not
			if( hasValidPattern() && m_pattern->instrumentTrack()->pianoModel()->isKeyPressed( key ) )
			{
				p.drawPixmap( PIANO_X, y - ( first_white_key_height -
						m_whiteKeySmallHeight ) -
						m_whiteKeySmallHeight/2 - 1 -
						m_blackKeyHeight, BLACK_KEY_WIDTH, m_blackKeyHeight, *s_blackKeyPressedPm );
			}
		    else
			{
				p.drawPixmap( PIANO_X, y - ( first_white_key_height -
						m_whiteKeySmallHeight ) -
						m_whiteKeySmallHeight/2 - 1 -
						m_blackKeyHeight, BLACK_KEY_WIDTH, m_blackKeyHeight, *s_blackKeyPm );
			}
			// update y-pos
			y -= m_whiteKeyBigHeight;
			// reset white-counter
			white_cnt = 0;
		}
		else
		{
			// simple workaround for increasing x if there were
			// two white keys (e.g. between E and F)
			++white_cnt;
			if( white_cnt > 1 )
			{
				y -= m_whiteKeyBigHeight/2;
			}
		}

		++key;
	}


	// erase the area below the piano, because there might be keys that
	// should be only half-visible
	p.fillRect( QRect( 0, key_line_y,
			WHITE_KEY_WIDTH, noteEditBottom() - key_line_y ), bgColor );

	// display note editing info
	QFont f = p.font();
	f.setBold( false );
	p.setFont( pointSize<10>( f ) );
	p.setPen( noteModeColor() );
	p.drawText( QRect( 0, key_line_y,
					  WHITE_KEY_WIDTH, noteEditBottom() - key_line_y ),
			   Qt::AlignCenter | Qt::TextWordWrap,
			   m_nemStr.at( m_noteEditMode ) + ":" );

	// set clipping area, because we are not allowed to paint over
	// keyboard...
	p.setClipRect( WHITE_KEY_WIDTH, PR_TOP_MARGIN,
				width() - WHITE_KEY_WIDTH,
				height() - PR_TOP_MARGIN - PR_BOTTOM_MARGIN );

	// draw the grid
	if( hasValidPattern() )
	{
		int q, x, tick;

		if( m_zoomingModel.value() > 3 )
		{
			// If we're over 100% zoom, we allow all quantization level grids
			q = quantization();
		}
		else if( quantization() % 3 != 0 )
		{
			// If we're under 100% zoom, we allow quantization grid up to 1/24 for triplets
			// to ensure a dense doesn't fill out the background
			q = quantization() < 8 ? 8 : quantization();
		}
		else {
			// If we're under 100% zoom, we allow quantization grid up to 1/32 for normal notes
			q = quantization() < 6 ? 6 : quantization();
		}

		// First we draw the vertical quantization lines
		for( tick = m_currentPosition - m_currentPosition % q, x = xCoordOfTick( tick );
			x <= width(); tick += q, x = xCoordOfTick( tick ) )
		{
			p.setPen( lineColor() );
			p.drawLine( x, PR_TOP_MARGIN, x, height() - PR_BOTTOM_MARGIN );
		}

		// Draw horizontal lines
		key = m_startKey;
		for( int y = key_line_y - 1; y > PR_TOP_MARGIN;
				y -= m_keyLineHeight )
		{
			if( static_cast<Keys>( key % KeysPerOctave ) == Key_C )
			{
				// C note gets accented
				p.setPen( beatLineColor() );
			}
			else
			{
				p.setPen( lineColor() );
			}
			p.drawLine( WHITE_KEY_WIDTH, y, width(), y );
			++key;
		}


		// Draw alternating shades on bars
		float timeSignature = static_cast<float>( Engine::getSong()->getTimeSigModel().getNumerator() )
				/ static_cast<float>( Engine::getSong()->getTimeSigModel().getDenominator() );
		float zoomFactor = m_zoomLevels[m_zoomingModel.value()];
		//the bars which disappears at the left side by scrolling
		int leftBars = m_currentPosition * zoomFactor / MidiTime::ticksPerBar();

		//iterates the visible bars and draw the shading on uneven bars
		for( int x = WHITE_KEY_WIDTH, barCount = leftBars; x < width() + m_currentPosition * zoomFactor / timeSignature; x += m_ppb, ++barCount )
		{
			if( ( barCount + leftBars )  % 2 != 0 )
			{
				p.fillRect( x - m_currentPosition * zoomFactor / timeSignature, PR_TOP_MARGIN, m_ppb,
					height() - ( PR_BOTTOM_MARGIN + PR_TOP_MARGIN ), backgroundShade() );
			}
		}

		// Draw the vertical beat lines
		int ticksPerBeat = DefaultTicksPerBar /
			Engine::getSong()->getTimeSigModel().getDenominator();

		for( tick = m_currentPosition - m_currentPosition % ticksPerBeat,
			x = xCoordOfTick( tick ); x <= width();
			tick += ticksPerBeat, x = xCoordOfTick( tick ) )
		{
			p.setPen( beatLineColor() );
			p.drawLine( x, PR_TOP_MARGIN, x, height() - PR_BOTTOM_MARGIN );
		}

		// Draw the vertical bar lines
		for( tick = m_currentPosition - m_currentPosition % MidiTime::ticksPerBar(),
			x = xCoordOfTick( tick ); x <= width();
			tick += MidiTime::ticksPerBar(), x = xCoordOfTick( tick ) )
		{
			p.setPen( barLineColor() );
			p.drawLine( x, PR_TOP_MARGIN, x, height() - PR_BOTTOM_MARGIN );
		}

		// draw marked semitones after the grid
		for( int i = 0; i < m_markedSemiTones.size(); i++ )
		{
			const int key_num = m_markedSemiTones.at( i );
			const int y = key_line_y + 5
				- m_keyLineHeight * ( key_num - m_startKey + 1 );

			if( y > key_line_y )
			{
				break;
			}

			p.fillRect( WHITE_KEY_WIDTH + 1, y - m_keyLineHeight / 2, width() - 10, m_keyLineHeight + 1,
				    markedSemitoneColor() );
		}
	}

	// following code draws all notes in visible area
	// and the note editing stuff (volume, panning, etc)

	// setup selection-vars
	int sel_pos_start = m_selectStartTick;
	int sel_pos_end = m_selectStartTick+m_selectedTick;
	if( sel_pos_start > sel_pos_end )
	{
		qSwap<int>( sel_pos_start, sel_pos_end );
	}

	int sel_key_start = m_selectStartKey - m_startKey + 1;
	int sel_key_end = sel_key_start + m_selectedKeys;
	if( sel_key_start > sel_key_end )
	{
		qSwap<int>( sel_key_start, sel_key_end );
	}

	int y_base = key_line_y - 1;
	if( hasValidPattern() )
	{
		p.setClipRect( WHITE_KEY_WIDTH, PR_TOP_MARGIN,
				width() - WHITE_KEY_WIDTH,
				height() - PR_TOP_MARGIN );

		const int visible_keys = ( key_line_y-keyAreaTop() ) /
							m_keyLineHeight + 2;

		QPolygonF editHandles;

		// -- Begin ghost pattern
		if( !m_ghostNotes.empty() )
		{
			for( const Note *note : m_ghostNotes )
			{
				int len_ticks = note->length();

				if( len_ticks == 0 )
				{
					continue;
				}
				else if( len_ticks < 0 )
				{
					len_ticks = 4;
				}
				const int key = note->key() - m_startKey + 1;

				int pos_ticks = note->pos();

				int note_width = len_ticks * m_ppb / MidiTime::ticksPerBar();
				const int x = ( pos_ticks - m_currentPosition ) *
						m_ppb / MidiTime::ticksPerBar();
				// skip this note if not in visible area at all
				if( !( x + note_width >= 0 && x <= width() - WHITE_KEY_WIDTH ) )
				{
					continue;
				}

				// is the note in visible area?
				if( key > 0 && key <= visible_keys )
				{

					// we've done and checked all, let's draw the
					// note
					drawNoteRect( p, x + WHITE_KEY_WIDTH,
							y_base - key * m_keyLineHeight,
									note_width, note, ghostNoteColor(), ghostNoteTextColor(), selectedNoteColor(),
									ghostNoteOpacity(), ghostNoteBorders(), drawNoteNames );
				}

			}
		}
		// -- End ghost pattern

		for( const Note *note : m_pattern->notes() )
		{
			int len_ticks = note->length();

			if( len_ticks == 0 )
			{
				continue;
			}
			else if( len_ticks < 0 )
			{
				len_ticks = 4;
			}

			const int key = note->key() - m_startKey + 1;

			int pos_ticks = note->pos();

			int note_width = len_ticks * m_ppb / MidiTime::ticksPerBar();
			const int x = ( pos_ticks - m_currentPosition ) *
					m_ppb / MidiTime::ticksPerBar();
			// skip this note if not in visible area at all
			if( !( x + note_width >= 0 && x <= width() - WHITE_KEY_WIDTH ) )
			{
				continue;
			}

			// is the note in visible area?
			if( key > 0 && key <= visible_keys )
			{

				// we've done and checked all, let's draw the
				// note
				drawNoteRect( p, x + WHITE_KEY_WIDTH,
						y_base - key * m_keyLineHeight,
								note_width, note, noteColor(), noteTextColor(), selectedNoteColor(),
								noteOpacity(), noteBorders(), drawNoteNames );
			}

			// draw note editing stuff
			int editHandleTop = 0;
			if( m_noteEditMode == NoteEditVolume )
			{
				QColor color = barColor().lighter( 30 + ( note->getVolume() * 90 / MaxVolume ) );
				if( note->selected() )
				{
					color = selectedNoteColor();
				}
				p.setPen( QPen( color, NOTE_EDIT_LINE_WIDTH ) );

				editHandleTop = noteEditBottom() -
					( (float)( note->getVolume() - MinVolume ) ) /
					( (float)( MaxVolume - MinVolume ) ) *
					( (float)( noteEditBottom() - noteEditTop() ) );

				p.drawLine( QLineF ( noteEditLeft() + x + 0.5, editHandleTop + 0.5,
							noteEditLeft() + x + 0.5, noteEditBottom() + 0.5 ) );

			}
			else if( m_noteEditMode == NoteEditPanning )
			{
				QColor color = noteColor();
				if( note->selected() )
				{
					color = selectedNoteColor();
				}

				p.setPen( QPen( color, NOTE_EDIT_LINE_WIDTH ) );

				editHandleTop = noteEditBottom() -
					( (float)( note->getPanning() - PanningLeft ) ) /
					( (float)( (PanningRight - PanningLeft ) ) ) *
					( (float)( noteEditBottom() - noteEditTop() ) );

				p.drawLine( QLine( noteEditLeft() + x, noteEditTop() +
						( (float)( noteEditBottom() - noteEditTop() ) ) / 2.0f,
						    noteEditLeft() + x , editHandleTop ) );
			}
			editHandles << QPoint ( x + noteEditLeft(),
						editHandleTop );

			if( note->hasDetuningInfo() )
			{
				drawDetuningInfo( p, note,
					x + WHITE_KEY_WIDTH,
					y_base - key * m_keyLineHeight );
				p.setClipRect(WHITE_KEY_WIDTH, PR_TOP_MARGIN,
					width() - WHITE_KEY_WIDTH,
					height() - PR_TOP_MARGIN);
			}
		}

		//draw current step recording notes
		for( const Note *note : m_stepRecorder.getCurStepNotes() )
		{
			int len_ticks = note->length();

			if( len_ticks == 0 )
			{
				continue;
			}

			const int key = note->key() - m_startKey + 1;

			int pos_ticks = note->pos();

			int note_width = len_ticks * m_ppb / MidiTime::ticksPerBar();
			const int x = ( pos_ticks - m_currentPosition ) *
					m_ppb / MidiTime::ticksPerBar();
			// skip this note if not in visible area at all
			if( !( x + note_width >= 0 && x <= width() - WHITE_KEY_WIDTH ) )
			{
				continue;
			}

			// is the note in visible area?
			if( key > 0 && key <= visible_keys )
			{

				// we've done and checked all, let's draw the note
				drawNoteRect( p, x + WHITE_KEY_WIDTH,
						y_base - key * m_keyLineHeight,
								note_width, note, m_stepRecorder.curStepNoteColor(), noteTextColor(), selectedNoteColor(),
								noteOpacity(), noteBorders(), drawNoteNames );
			}
		}

		p.setPen( QPen( noteColor(), NOTE_EDIT_LINE_WIDTH + 2 ) );
		p.drawPoints( editHandles );

	}
	else
	{
		QFont f = p.font();
		f.setBold( true );
		p.setFont( pointSize<14>( f ) );
		p.setPen( QApplication::palette().color( QPalette::Active,
							QPalette::BrightText ) );
		p.drawText( WHITE_KEY_WIDTH + 20, PR_TOP_MARGIN + 40,
				tr( "Please open a pattern by double-clicking "
								"on it!" ) );
	}

	p.setClipRect( WHITE_KEY_WIDTH, PR_TOP_MARGIN, width() -
				WHITE_KEY_WIDTH, height() - PR_TOP_MARGIN -
					m_notesEditHeight - PR_BOTTOM_MARGIN );

	// now draw selection-frame
	int x = ( ( sel_pos_start - m_currentPosition ) * m_ppb ) /
						MidiTime::ticksPerBar();
	int w = ( ( ( sel_pos_end - m_currentPosition ) * m_ppb ) /
						MidiTime::ticksPerBar() ) - x;
	int y = (int) y_base - sel_key_start * m_keyLineHeight;
	int h = (int) y_base - sel_key_end * m_keyLineHeight - y;
	p.setPen( selectedNoteColor() );
	p.setBrush( Qt::NoBrush );
	p.drawRect( x + WHITE_KEY_WIDTH, y, w, h );

	// TODO: Get this out of paint event
	int l = ( hasValidPattern() )? (int) m_pattern->length() : 0;

	// reset scroll-range
	if( m_leftRightScroll->maximum() != l )
	{
		m_leftRightScroll->setRange( 0, l );
		m_leftRightScroll->setPageStep( l );
	}

	// set line colors
	QColor editAreaCol = QColor( lineColor() );
	QColor currentKeyCol = QColor( beatLineColor() );

	editAreaCol.setAlpha( 64 );
	currentKeyCol.setAlpha( 64 );

	// horizontal line for the key under the cursor
	if( hasValidPattern() )
	{
		int key_num = getKey( mapFromGlobal( QCursor::pos() ).y() );
		p.fillRect( 10, key_line_y + 3 - m_keyLineHeight *
					( key_num - m_startKey + 1 ), width() - 10, m_keyLineHeight - 7, currentKeyCol );
	}

	// bar to resize note edit area
	p.setClipRect( 0, 0, width(), height() );
	p.fillRect( QRect( 0, key_line_y,
					width()-PR_RIGHT_MARGIN, NOTE_EDIT_RESIZE_BAR ), editAreaCol );

	const QPixmap * cursor = NULL;
	// draw current edit-mode-icon below the cursor
	switch( m_editMode )
	{
		case ModeDraw:
			if( m_mouseDownRight )
			{
				cursor = s_toolErase;
			}
			else if( m_action == ActionMoveNote )
			{
				cursor = s_toolMove;
			}
			else
			{
				cursor = s_toolDraw;
			}
			break;
		case ModeErase: cursor = s_toolErase; break;
		case ModeSelect: cursor = s_toolSelect; break;
		case ModeEditDetuning: cursor = s_toolOpen; break;
	}
	QPoint mousePosition = mapFromGlobal( QCursor::pos() );
	if( cursor != NULL && mousePosition.y() > keyAreaTop() && mousePosition.x() > noteEditLeft())
	{
		p.drawPixmap( mousePosition + QPoint( 8, 8 ), *cursor );
	}
}




// responsible for moving/resizing scrollbars after window-resizing
void PianoRoll::resizeEvent(QResizeEvent * re)
{
	m_leftRightScroll->setGeometry( WHITE_KEY_WIDTH,
								      height() -
								SCROLLBAR_SIZE,
					width()-WHITE_KEY_WIDTH,
							SCROLLBAR_SIZE );
	updateYScroll();

	Engine::getSong()->getPlayPos( Song::Mode_PlayPattern
					).m_timeLine->setFixedWidth( width() );

	update();
}




void PianoRoll::wheelEvent(QWheelEvent * we )
{
	we->accept();
	// handle wheel events for note edit area - for editing note vol/pan with mousewheel
	if( we->x() > noteEditLeft() && we->x() < noteEditRight()
	&& we->y() > noteEditTop() && we->y() < noteEditBottom() )
	{
		if (!hasValidPattern()) {return;}
		// get values for going through notes
		int pixel_range = 8;
		int x = we->x() - WHITE_KEY_WIDTH;
		int ticks_start = ( x - pixel_range / 2 ) *
					MidiTime::ticksPerBar() / m_ppb + m_currentPosition;
		int ticks_end = ( x + pixel_range / 2 ) *
					MidiTime::ticksPerBar() / m_ppb + m_currentPosition;

		// When alt is pressed we only edit the note under the cursor
		bool altPressed = we->modifiers() & Qt::AltModifier;
		// go through notes to figure out which one we want to change
		NoteVector nv;
		for ( Note * i : m_pattern->notes() )
		{
			if( i->withinRange( ticks_start, ticks_end ) || ( i->selected() && !altPressed ) )
			{
				nv += i;
			}
		}
		if( nv.size() > 0 )
		{
			const int step = we->delta() > 0 ? 1 : -1;
			if( m_noteEditMode == NoteEditVolume )
			{
				for ( Note * n : nv )
				{
					volume_t vol = qBound<int>( MinVolume, n->getVolume() + step, MaxVolume );
					n->setVolume( vol );
				}
				bool allVolumesEqual = std::all_of( nv.begin(), nv.end(),
					[nv](const Note *note)
					{
						return note->getVolume() == nv[0]->getVolume();
					});
				if ( allVolumesEqual )
				{
					// show the volume hover-text only if all notes have the
					// same volume
					showVolTextFloat( nv[0]->getVolume(), we->pos(), 1000 );
				}
			}
			else if( m_noteEditMode == NoteEditPanning )
			{
				for ( Note * n : nv )
				{
					panning_t pan = qBound<int>( PanningLeft, n->getPanning() + step, PanningRight );
					n->setPanning( pan );
				}
				bool allPansEqual = std::all_of( nv.begin(), nv.end(),
					[nv](const Note *note)
					{
						return note->getPanning() == nv[0]->getPanning();
					});
				if ( allPansEqual )
				{
					// show the pan hover-text only if all notes have the same
					// panning
					showPanTextFloat( nv[0]->getPanning(), we->pos(), 1000 );
				}
			}
			update();
		}
	}

	// not in note edit area, so handle scrolling/zooming and quantization change
	else
	if( we->modifiers() & Qt::ControlModifier && we->modifiers() & Qt::AltModifier )
	{
		int q = m_quantizeModel.value();
		if( we->delta() > 0 )
		{
			q--;
		}
		else if( we->delta() < 0 )
		{
			q++;
		}
		q = qBound( 0, q, m_quantizeModel.size() - 1 );
		m_quantizeModel.setValue( q );
	}
	else if( we->modifiers() & Qt::ControlModifier && we->modifiers() & Qt::ShiftModifier )
	{
		int l = m_noteLenModel.value();
		if( we->delta() > 0 )
		{
			l--;
		}
		else if( we->delta() < 0 )
		{
			l++;
		}
		l = qBound( 0, l, m_noteLenModel.size() - 1 );
		m_noteLenModel.setValue( l );
	}
	else if( we->modifiers() & Qt::ControlModifier )
	{
		int z = m_zoomingModel.value();
		if( we->delta() > 0 )
		{
			z++;
		}
		else if( we->delta() < 0 )
		{
			z--;
		}
		z = qBound( 0, z, m_zoomingModel.size() - 1 );

		int x = (we->x() - WHITE_KEY_WIDTH)* MidiTime::ticksPerBar();
		// ticks based on the mouse x-position where the scroll wheel was used
		int ticks = x / m_ppb;
		// what would be the ticks in the new zoom level on the very same mouse x
		int newTicks = x / (DEFAULT_PR_PPB * m_zoomLevels[z]);
		// scroll so the tick "selected" by the mouse x doesn't move on the screen
		m_leftRightScroll->setValue(m_leftRightScroll->value() + ticks - newTicks);
		// update combobox with zooming-factor
		m_zoomingModel.setValue( z );
	}
	else if( we->modifiers() & Qt::ShiftModifier
			 || we->orientation() == Qt::Horizontal )
	{
		m_leftRightScroll->setValue( m_leftRightScroll->value() -
							we->delta() * 2 / 15 );
	}
	else
	{
		m_topBottomScroll->setValue( m_topBottomScroll->value() -
							we->delta() / 30 );
	}
}




void PianoRoll::focusOutEvent( QFocusEvent * )
{
	if( hasValidPattern() )
	{
		for( int i = 0; i < NumKeys; ++i )
		{
			m_pattern->instrumentTrack()->pianoModel()->midiEventProcessor()->processInEvent( MidiEvent( MidiNoteOff, -1, i, 0 ) );
			m_pattern->instrumentTrack()->pianoModel()->setKeyState( i, false );
		}
	}
	m_editMode = m_ctrlMode;
	update();
}




int PianoRoll::getKey(int y ) const
{
	int key_line_y = keyAreaBottom() - 1;
	// pressed key on piano
	int key_num = ( key_line_y - y ) / m_keyLineHeight;
	key_num += m_startKey;

	// some range-checking-stuff
	if( key_num < 0 )
	{
		key_num = 0;
	}

	if( key_num >= KeysPerOctave * NumOctaves )
	{
		key_num = KeysPerOctave * NumOctaves - 1;
	}

	return key_num;
}

QList<int> PianoRoll::getAllOctavesForKey( int keyToMirror ) const
{
	QList<int> keys;

	for (int i=keyToMirror % KeysPerOctave; i < NumKeys; i += KeysPerOctave)
	{
		keys.append(i);
	}

	return keys;
}

Song::PlayModes PianoRoll::desiredPlayModeForAccompany() const
{
	if( m_pattern->getTrack()->trackContainer() ==
					Engine::getBBTrackContainer() )
	{
		return Song::Mode_PlayBB;
	}
	return Song::Mode_PlaySong;
}




void PianoRoll::play()
{
	if( ! hasValidPattern() )
	{
		return;
	}

	if( Engine::getSong()->playMode() != Song::Mode_PlayPattern )
	{
		Engine::getSong()->playPattern( m_pattern );
	}
	else
	{
		Engine::getSong()->togglePause();
	}
}




void PianoRoll::record()
{
	if( Engine::getSong()->isPlaying() )
	{
		stop();
	}
	if( m_recording || ! hasValidPattern() )
	{
		return;
	}

	m_pattern->addJournalCheckPoint();
	m_recording = true;

	Engine::getSong()->playPattern( m_pattern, false );
}




void PianoRoll::recordAccompany()
{
	if( Engine::getSong()->isPlaying() )
	{
		stop();
	}
	if( m_recording || ! hasValidPattern() )
	{
		return;
	}

	m_pattern->addJournalCheckPoint();
	m_recording = true;

	if( m_pattern->getTrack()->trackContainer() == Engine::getSong() )
	{
		Engine::getSong()->playSong();
	}
	else
	{
		Engine::getSong()->playBB();
	}
}




bool PianoRoll::toggleStepRecording()
{
	if(m_stepRecorder.isRecording())
	{
		m_stepRecorder.stop();
	}
	else
	{
		if(hasValidPattern())
		{
			if(Engine::getSong()->isPlaying())
			{
				m_stepRecorder.start(0, newNoteLen());
			}
			else
			{
				m_stepRecorder.start(
					Engine::getSong()->getPlayPos(
						Song::Mode_PlayPattern), newNoteLen());
			}
		}
	}

	return m_stepRecorder.isRecording();;
}




void PianoRoll::stop()
{
	Engine::getSong()->stop();
	m_recording = false;
	m_scrollBack = ( m_timeLine->autoScroll() == TimeLineWidget::AutoScrollEnabled );
}




void PianoRoll::startRecordNote(const Note & n )
{
	if(hasValidPattern())
	{
		if( m_recording &&
			Engine::getSong()->isPlaying() &&
			(Engine::getSong()->playMode() == desiredPlayModeForAccompany() ||
			Engine::getSong()->playMode() == Song::Mode_PlayPattern ))
		{
			MidiTime sub;
			if( Engine::getSong()->playMode() == Song::Mode_PlaySong )
			{
				sub = m_pattern->startPosition();
			}
			Note n1( 1, Engine::getSong()->getPlayPos(
						Engine::getSong()->playMode() ) - sub,
					n.key(), n.getVolume(), n.getPanning() );
			if( n1.pos() >= 0 )
			{
				m_recordingNotes << n1;
			}
		}
		else if (m_stepRecorder.isRecording())
		{
			m_stepRecorder.notePressed(n);
		}
	}
}




void PianoRoll::finishRecordNote(const Note & n )
{
	if(hasValidPattern())
	{
		if( m_recording &&
			Engine::getSong()->isPlaying() &&
				( Engine::getSong()->playMode() ==
						desiredPlayModeForAccompany() ||
					Engine::getSong()->playMode() ==
						Song::Mode_PlayPattern ) )
		{
			for( QList<Note>::Iterator it = m_recordingNotes.begin();
						it != m_recordingNotes.end(); ++it )
			{
				if( it->key() == n.key() )
				{
					Note n1( n.length(), it->pos(),
							it->key(), it->getVolume(),
							it->getPanning() );
					n1.quantizeLength( quantization() );
					m_pattern->addNote( n1 );
					update();
					m_recordingNotes.erase( it );
					break;
				}
			}
		}
		else if (m_stepRecorder.isRecording())
		{
			m_stepRecorder.noteReleased(n);
		}
	}
}




void PianoRoll::horScrolled(int new_pos )
{
	m_currentPosition = new_pos;
	m_stepRecorderWidget.setCurrentPosition(m_currentPosition);
	emit positionChanged( m_currentPosition );
	update();
}




void PianoRoll::verScrolled( int new_pos )
{
	// revert value
	m_startKey = m_totalKeysToScroll - new_pos;

	update();
}




void PianoRoll::setEditMode(int mode)
{
	m_ctrlMode = m_editMode = (EditModes) mode;
}




void PianoRoll::selectAll()
{
	if( ! hasValidPattern() )
	{
		return;
	}

	// if first_time = true, we HAVE to set the vars for select
	bool first_time = true;

	for( const Note *note : m_pattern->notes() )
	{
		int len_ticks = static_cast<int>( note->length() ) > 0 ?
				static_cast<int>( note->length() ) : 1;

		const int key = note->key();

		int pos_ticks = note->pos();
		if( key <= m_selectStartKey || first_time )
		{
			// if we move start-key down, we have to add
			// the difference between old and new start-key
			// to m_selectedKeys, otherwise the selection
			// is just moved down...
			m_selectedKeys += m_selectStartKey
							- ( key - 1 );
			m_selectStartKey = key - 1;
		}
		if( key >= m_selectedKeys + m_selectStartKey ||
							first_time )
		{
			m_selectedKeys = key - m_selectStartKey;
		}
		if( pos_ticks < m_selectStartTick ||
							first_time )
		{
			m_selectStartTick = pos_ticks;
		}
		if( pos_ticks + len_ticks >
			m_selectStartTick + m_selectedTick ||
							first_time )
		{
			m_selectedTick = pos_ticks +
						len_ticks -
						m_selectStartTick;
		}
		first_time = false;
	}
}




// returns vector with pointers to all selected notes
NoteVector PianoRoll::getSelectedNotes() const
{
	NoteVector selectedNotes;

	if (hasValidPattern())
	{
		for( Note *note : m_pattern->notes() )
		{
			if( note->selected() )
			{
				selectedNotes.push_back( note );
			}
		}
	}
	return selectedNotes;
}

// selects all notess associated with m_lastKey
void PianoRoll::selectNotesOnKey()
{
	if (hasValidPattern()) {
		for (Note * note : m_pattern->notes()) {
			if (note->key() == m_lastKey) {
				note->setSelected(true);
			}
		}
	}
}

void PianoRoll::enterValue( NoteVector* nv )
{

	if( m_noteEditMode == NoteEditVolume )
	{
		bool ok;
		int new_val;
		new_val = QInputDialog::getInt(	this, "Piano roll: note velocity",
					tr( "Please enter a new value between %1 and %2:" ).
						arg( MinVolume ).arg( MaxVolume ),
					(*nv)[0]->getVolume(),
					MinVolume, MaxVolume, 1, &ok );

		if( ok )
		{
			for ( Note * n : *nv )
			{
				n->setVolume( new_val );
			}
			m_lastNoteVolume = new_val;
		}
	}
	else if( m_noteEditMode == NoteEditPanning )
	{
		bool ok;
		int new_val;
		new_val = QInputDialog::getInt(	this, "Piano roll: note panning",
					tr( "Please enter a new value between %1 and %2:" ).
							arg( PanningLeft ).arg( PanningRight ),
						(*nv)[0]->getPanning(),
						PanningLeft, PanningRight, 1, &ok );

		if( ok )
		{
			for ( Note * n : *nv )
			{
				n->setPanning( new_val );
			}
			m_lastNotePanning = new_val;
		}

	}
}


void PianoRoll::updateYScroll()
{
	m_topBottomScroll->setGeometry(width() - SCROLLBAR_SIZE, PR_TOP_MARGIN,
						SCROLLBAR_SIZE,
						height() - PR_TOP_MARGIN -
						SCROLLBAR_SIZE);

	int total_pixels = m_octaveHeight * NumOctaves - (height() -
					PR_TOP_MARGIN - PR_BOTTOM_MARGIN -
							m_notesEditHeight);
	m_totalKeysToScroll = total_pixels * KeysPerOctave / m_octaveHeight;

	m_topBottomScroll->setRange(0, m_totalKeysToScroll);

	if(m_startKey > m_totalKeysToScroll)
	{
		m_startKey = m_totalKeysToScroll;
	}
	m_topBottomScroll->setValue(m_totalKeysToScroll - m_startKey);
}


void PianoRoll::copyToClipboard( const NoteVector & notes ) const
{
	DataFile dataFile( DataFile::ClipboardData );
	QDomElement note_list = dataFile.createElement( "note-list" );
	dataFile.content().appendChild( note_list );

	MidiTime start_pos( notes.front()->pos().getBar(), 0 );
	for( const Note *note : notes )
	{
		Note clip_note( *note );
		clip_note.setPos( clip_note.pos( start_pos ) );
		clip_note.saveState( dataFile, note_list );
	}

	QMimeData * clip_content = new QMimeData;
	clip_content->setData( Clipboard::mimeType(), dataFile.toString().toUtf8() );
	QApplication::clipboard()->setMimeData( clip_content,
							QClipboard::Clipboard );
}




void PianoRoll::copySelectedNotes()
{
	NoteVector selected_notes = getSelectedNotes();

	if( ! selected_notes.empty() )
	{
		copyToClipboard( selected_notes );
	}
}




void PianoRoll::cutSelectedNotes()
{
	if( ! hasValidPattern() )
	{
		return;
	}

	NoteVector selected_notes = getSelectedNotes();

	if( ! selected_notes.empty() )
	{
		m_pattern->addJournalCheckPoint();

		copyToClipboard( selected_notes );

		Engine::getSong()->setModified();

		for( Note *note : selected_notes )
		{
			// note (the memory of it) is also deleted by
			// pattern::removeNote(...) so we don't have to do that
			m_pattern->removeNote( note );
		}
	}

	update();
	gui->songEditor()->update();
}




void PianoRoll::pasteNotes()
{
	if( ! hasValidPattern() )
	{
		return;
	}

	QString value = QApplication::clipboard()
				->mimeData( QClipboard::Clipboard )
						->data( Clipboard::mimeType() );

	if( ! value.isEmpty() )
	{
		DataFile dataFile( value.toUtf8() );

		QDomNodeList list = dataFile.elementsByTagName( Note::classNodeName() );

		// remove selection and select the newly pasted notes
		clearSelectedNotes();

		if( ! list.isEmpty() )
		{
			m_pattern->addJournalCheckPoint();
		}

		for( int i = 0; ! list.item( i ).isNull(); ++i )
		{
			// create the note
			Note cur_note;
			cur_note.restoreState( list.item( i ).toElement() );
			cur_note.setPos( cur_note.pos() + Note::quantized( m_timeLine->pos(), quantization() ) );

			// select it
			cur_note.setSelected( true );

			// add to pattern
			m_pattern->addNote( cur_note, false );
		}

		// we only have to do the following lines if we pasted at
		// least one note...
		Engine::getSong()->setModified();
		update();
		gui->songEditor()->update();
	}
}




//Return false if no notes are deleted
bool PianoRoll::deleteSelectedNotes()
{
	if (!hasValidPattern()) { return false; }

	auto selectedNotes = getSelectedNotes();
	if (selectedNotes.empty()) { return false; }

	m_pattern->addJournalCheckPoint();

	for (Note* note: selectedNotes) { m_pattern->removeNote( note ); }

	Engine::getSong()->setModified();
	update();
	gui->songEditor()->update();
	return true;
}




void PianoRoll::autoScroll( const MidiTime & t )
{
	const int w = width() - WHITE_KEY_WIDTH;
	if( t > m_currentPosition + w * MidiTime::ticksPerBar() / m_ppb )
	{
		m_leftRightScroll->setValue( t.getBar() * MidiTime::ticksPerBar() );
	}
	else if( t < m_currentPosition )
	{
		MidiTime t2 = qMax( t - w * MidiTime::ticksPerBar() *
					MidiTime::ticksPerBar() / m_ppb, (tick_t) 0 );
		m_leftRightScroll->setValue( t2.getBar() * MidiTime::ticksPerBar() );
	}
	m_scrollBack = false;
}




void PianoRoll::updatePosition( const MidiTime & t )
{
	if( ( Engine::getSong()->isPlaying()
			&& Engine::getSong()->playMode() == Song::Mode_PlayPattern
			&& m_timeLine->autoScroll() == TimeLineWidget::AutoScrollEnabled
		) || m_scrollBack )
	{
		autoScroll( t );
	}
}




void PianoRoll::updatePositionAccompany( const MidiTime & t )
{
	Song * s = Engine::getSong();

	if( m_recording && hasValidPattern() &&
					s->playMode() != Song::Mode_PlayPattern )
	{
		MidiTime pos = t;
		if( s->playMode() != Song::Mode_PlayBB )
		{
			pos -= m_pattern->startPosition();
		}
		if( (int) pos > 0 )
		{
			s->getPlayPos( Song::Mode_PlayPattern ).setTicks( pos );
			autoScroll( pos );
		}
	}
}


void PianoRoll::updatePositionStepRecording( const MidiTime & t )
{
	if( m_stepRecorder.isRecording() )
	{
		autoScroll( t );
	}
}


void PianoRoll::zoomingChanged()
{
	m_ppb = m_zoomLevels[m_zoomingModel.value()] * DEFAULT_PR_PPB;

	assert( m_ppb > 0 );

	m_timeLine->setPixelsPerBar( m_ppb );
	m_stepRecorderWidget.setPixelsPerBar( m_ppb );

	update();
}


void PianoRoll::zoomingYChanged()
{
	m_keyLineHeight = m_zoomYLevels[m_zoomingYModel.value()] * DEFAULT_KEY_LINE_HEIGHT;
	m_octaveHeight = m_keyLineHeight * KeysPerOctave;
	m_whiteKeySmallHeight = round(m_keyLineHeight * 1.5);
	m_whiteKeyBigHeight = m_keyLineHeight * 2;
	m_blackKeyHeight = round(m_keyLineHeight * 1.3333);

	updateYScroll();
	update();
}


void PianoRoll::quantizeChanged()
{
	update();
}

void PianoRoll::noteLengthChanged()
{
	m_stepRecorder.setStepsLength(newNoteLen());
	update();
}


int PianoRoll::quantization() const
{
	if( m_quantizeModel.value() == 0 )
	{
		if( m_noteLenModel.value() > 0 )
		{
			return newNoteLen();
		}
		else
		{
			return DefaultTicksPerBar / 16;
		}
	}

	QString text = m_quantizeModel.currentText();
	return DefaultTicksPerBar / text.right( text.length() - 2 ).toInt();
}


void PianoRoll::quantizeNotes()
{
	if( ! hasValidPattern() )
	{
		return;
	}

	m_pattern->addJournalCheckPoint();

	NoteVector notes = getSelectedNotes();

	if( notes.empty() )
	{
		for( Note* n : m_pattern->notes() )
		{
			notes.push_back( n );
		}
	}

	for( Note* n : notes )
	{
		if( n->length() == MidiTime( 0 ) )
		{
			continue;
		}

		Note copy(*n);
		m_pattern->removeNote( n );
		copy.quantizePos( quantization() );
		m_pattern->addNote( copy );
	}

	update();
	gui->songEditor()->update();
	Engine::getSong()->setModified();
}




void PianoRoll::updateSemiToneMarkerMenu()
{
	const InstrumentFunctionNoteStacking::ChordTable& chord_table =
			InstrumentFunctionNoteStacking::ChordTable::getInstance();
	const InstrumentFunctionNoteStacking::Chord& scale =
			chord_table.getScaleByName( m_scaleModel.currentText() );
	const InstrumentFunctionNoteStacking::Chord& chord =
			chord_table.getChordByName( m_chordModel.currentText() );

	emit semiToneMarkerMenuScaleSetEnabled( ! scale.isEmpty() );
	emit semiToneMarkerMenuChordSetEnabled( ! chord.isEmpty() );
}




MidiTime PianoRoll::newNoteLen() const
{
	if( m_noteLenModel.value() == 0 )
	{
		return m_lenOfNewNotes;
	}

	QString text = m_noteLenModel.currentText();
	return DefaultTicksPerBar / text.right( text.length() - 2 ).toInt();
}




bool PianoRoll::mouseOverNote()
{
	return hasValidPattern() && noteUnderMouse() != NULL;
}




Note * PianoRoll::noteUnderMouse()
{
	QPoint pos = mapFromGlobal( QCursor::pos() );

	if( pos.x() <= WHITE_KEY_WIDTH
		|| pos.x() > width() - SCROLLBAR_SIZE
		|| pos.y() < PR_TOP_MARGIN
		|| pos.y() > keyAreaBottom() )
	{
		return NULL;
	}

	int key_num = getKey( pos.y() );
	int pos_ticks = ( pos.x() - WHITE_KEY_WIDTH ) *
			MidiTime::ticksPerBar() / m_ppb + m_currentPosition;

	// loop through whole note-vector...
	for( Note* const& note : m_pattern->notes() )
	{
		// and check whether the cursor is over an
		// existing note
		if( pos_ticks >= note->pos()
				&& pos_ticks <= note->endPos()
				&& note->key() == key_num
				&& note->length() > 0 )
		{
			return note;
		}
	}

	return NULL;
}




PianoRollWindow::PianoRollWindow() :
	Editor(true, true),
	m_editor(new PianoRoll())
{
	setCentralWidget( m_editor );

	m_playAction->setToolTip(tr( "Play/pause current pattern (Space)" ) );
	m_recordAction->setToolTip(tr( "Record notes from MIDI-device/channel-piano" ) );
	m_recordAccompanyAction->setToolTip( tr( "Record notes from MIDI-device/channel-piano while playing song or BB track" ) );
	m_toggleStepRecordingAction->setToolTip( tr( "Record notes from MIDI-device/channel-piano, one step at the time" ) );
	m_stopAction->setToolTip( tr( "Stop playing of current pattern (Space)" ) );

	DropToolBar *notesActionsToolBar = addDropToolBarToTop( tr( "Edit actions" ) );

	// init edit-buttons at the top
	ActionGroup* editModeGroup = new ActionGroup( this );
	QAction* drawAction = editModeGroup->addAction( embed::getIconPixmap( "edit_draw" ), tr( "Draw mode (Shift+D)" ) );
	QAction* eraseAction = editModeGroup->addAction( embed::getIconPixmap( "edit_erase" ), tr("Erase mode (Shift+E)" ) );
	QAction* selectAction = editModeGroup->addAction( embed::getIconPixmap( "edit_select" ), tr( "Select mode (Shift+S)" ) );
	QAction* pitchBendAction = editModeGroup->addAction( embed::getIconPixmap( "automation" ), tr("Pitch Bend mode (Shift+T)" ) );

	drawAction->setChecked( true );

	drawAction->setShortcut( Qt::SHIFT | Qt::Key_D );
	eraseAction->setShortcut( Qt::SHIFT | Qt::Key_E );
	selectAction->setShortcut( Qt::SHIFT | Qt::Key_S );
	pitchBendAction->setShortcut( Qt::SHIFT | Qt::Key_T );

	connect( editModeGroup, SIGNAL( triggered( int ) ), m_editor, SLOT( setEditMode( int ) ) );

	QAction* quantizeAction = new QAction(embed::getIconPixmap( "quantize" ), tr( "Quantize" ), this );
	connect( quantizeAction, SIGNAL( triggered() ), m_editor, SLOT( quantizeNotes() ) );

	notesActionsToolBar->addAction( drawAction );
	notesActionsToolBar->addAction( eraseAction );
	notesActionsToolBar->addAction( selectAction );
	notesActionsToolBar->addAction( pitchBendAction );
	notesActionsToolBar->addSeparator();
	notesActionsToolBar->addAction( quantizeAction );

	// Copy + paste actions
	DropToolBar *copyPasteActionsToolBar =  addDropToolBarToTop( tr( "Copy paste controls" ) );

	QAction* cutAction = new QAction(embed::getIconPixmap( "edit_cut" ),
								tr( "Cut (%1+X)" ).arg(UI_CTRL_KEY), this );

	QAction* copyAction = new QAction(embed::getIconPixmap( "edit_copy" ),
								 tr( "Copy (%1+C)" ).arg(UI_CTRL_KEY), this );

	QAction* pasteAction = new QAction(embed::getIconPixmap( "edit_paste" ),
					tr( "Paste (%1+V)" ).arg(UI_CTRL_KEY), this );

	cutAction->setShortcut( Qt::CTRL | Qt::Key_X );
	copyAction->setShortcut( Qt::CTRL | Qt::Key_C );
	pasteAction->setShortcut( Qt::CTRL | Qt::Key_V );

	connect( cutAction, SIGNAL( triggered() ), m_editor, SLOT( cutSelectedNotes() ) );
	connect( copyAction, SIGNAL( triggered() ), m_editor, SLOT( copySelectedNotes() ) );
	connect( pasteAction, SIGNAL( triggered() ), m_editor, SLOT( pasteNotes() ) );

	copyPasteActionsToolBar->addAction( cutAction );
	copyPasteActionsToolBar->addAction( copyAction );
	copyPasteActionsToolBar->addAction( pasteAction );


	DropToolBar *timeLineToolBar = addDropToolBarToTop( tr( "Timeline controls" ) );
	m_editor->m_timeLine->addToolButtons( timeLineToolBar );


	addToolBarBreak();


	DropToolBar *zoomAndNotesToolBar = addDropToolBarToTop( tr( "Zoom and note controls" ) );

	QLabel * zoom_lbl = new QLabel( m_toolBar );
	zoom_lbl->setPixmap( embed::getIconPixmap( "zoom_x" ) );

	m_zoomingComboBox = new ComboBox( m_toolBar );
	m_zoomingComboBox->setModel( &m_editor->m_zoomingModel );
	m_zoomingComboBox->setFixedSize( 64, 22 );
	m_zoomingComboBox->setToolTip( tr( "Horizontal zooming") );

	QLabel * zoom_y_lbl = new QLabel(m_toolBar);
	zoom_y_lbl->setPixmap(embed::getIconPixmap("zoom_y"));

	m_zoomingYComboBox = new ComboBox(m_toolBar);
	m_zoomingYComboBox->setModel(&m_editor->m_zoomingYModel);
	m_zoomingYComboBox->setFixedSize(64, 22);
	m_zoomingYComboBox->setToolTip(tr("Vertical zooming"));

	// setup quantize-stuff
	QLabel * quantize_lbl = new QLabel( m_toolBar );
	quantize_lbl->setPixmap( embed::getIconPixmap( "quantize" ) );

	m_quantizeComboBox = new ComboBox( m_toolBar );
	m_quantizeComboBox->setModel( &m_editor->m_quantizeModel );
	m_quantizeComboBox->setFixedSize( 64, 22 );
	m_quantizeComboBox->setToolTip( tr( "Quantization") );

	// setup note-len-stuff
	QLabel * note_len_lbl = new QLabel( m_toolBar );
	note_len_lbl->setPixmap( embed::getIconPixmap( "note" ) );

	m_noteLenComboBox = new ComboBox( m_toolBar );
	m_noteLenComboBox->setModel( &m_editor->m_noteLenModel );
	m_noteLenComboBox->setFixedSize( 105, 22 );
	m_noteLenComboBox->setToolTip( tr( "Note length") );

	// setup scale-stuff
	QLabel * scale_lbl = new QLabel( m_toolBar );
	scale_lbl->setPixmap( embed::getIconPixmap( "scale" ) );

	m_scaleComboBox = new ComboBox( m_toolBar );
	m_scaleComboBox->setModel( &m_editor->m_scaleModel );
	m_scaleComboBox->setFixedSize( 105, 22 );
	m_scaleComboBox->setToolTip( tr( "Scale") );

	// setup chord-stuff
	QLabel * chord_lbl = new QLabel( m_toolBar );
	chord_lbl->setPixmap( embed::getIconPixmap( "chord" ) );

	m_chordComboBox = new ComboBox( m_toolBar );
	m_chordComboBox->setModel( &m_editor->m_chordModel );
	m_chordComboBox->setFixedSize( 105, 22 );
	m_chordComboBox->setToolTip( tr( "Chord" ) );

	// -- Clear ghost pattern button
	m_clearGhostButton = new QPushButton( m_toolBar );
	m_clearGhostButton->setIcon( embed::getIconPixmap( "clear_ghost_note" ) );
	m_clearGhostButton->setToolTip( tr( "Clear ghost notes" ) );
	m_clearGhostButton->setEnabled( false );
	connect( m_clearGhostButton, SIGNAL( clicked() ), m_editor, SLOT( clearGhostPattern() ) );
	connect( m_editor, SIGNAL( ghostPatternSet( bool ) ), this, SLOT( ghostPatternSet( bool ) ) );

	zoomAndNotesToolBar->addWidget( zoom_lbl );
	zoomAndNotesToolBar->addWidget( m_zoomingComboBox );

	zoomAndNotesToolBar->addWidget(zoom_y_lbl);
	zoomAndNotesToolBar->addWidget(m_zoomingYComboBox);

	zoomAndNotesToolBar->addSeparator();
	zoomAndNotesToolBar->addWidget( quantize_lbl );
	zoomAndNotesToolBar->addWidget( m_quantizeComboBox );

	zoomAndNotesToolBar->addSeparator();
	zoomAndNotesToolBar->addWidget( note_len_lbl );
	zoomAndNotesToolBar->addWidget( m_noteLenComboBox );

	zoomAndNotesToolBar->addSeparator();
	zoomAndNotesToolBar->addWidget( scale_lbl );
	zoomAndNotesToolBar->addWidget( m_scaleComboBox );

	zoomAndNotesToolBar->addSeparator();
	zoomAndNotesToolBar->addWidget( chord_lbl );
	zoomAndNotesToolBar->addWidget( m_chordComboBox );

	zoomAndNotesToolBar->addSeparator();
	zoomAndNotesToolBar->addWidget( m_clearGhostButton );

	// setup our actual window
	setFocusPolicy( Qt::StrongFocus );
	setFocus();
	setWindowIcon( embed::getIconPixmap( "piano" ) );
	setCurrentPattern( NULL );

	// Connections
	connect( m_editor, SIGNAL( currentPatternChanged() ), this, SIGNAL( currentPatternChanged() ) );
	connect( m_editor, SIGNAL( currentPatternChanged() ), this, SLOT( updateAfterPatternChange() ) );
}




const Pattern* PianoRollWindow::currentPattern() const
{
	return m_editor->currentPattern();
}




void PianoRollWindow::setGhostPattern( Pattern* pattern )
{
	m_editor->setGhostPattern( pattern );
}




void PianoRollWindow::setCurrentPattern( Pattern* pattern )
{
	m_editor->setCurrentPattern( pattern );

	if ( pattern )
	{
		setWindowTitle( tr( "Piano-Roll - %1" ).arg( pattern->name() ) );
		connect( pattern->instrumentTrack(), SIGNAL( nameChanged() ), this, SLOT( updateAfterPatternChange()) );
		connect( pattern, SIGNAL( dataChanged() ), this, SLOT( updateAfterPatternChange() ) );
	}
	else
	{
		setWindowTitle( tr( "Piano-Roll - no pattern" ) );
	}
}




bool PianoRollWindow::isRecording() const
{
	return m_editor->isRecording();
}




int PianoRollWindow::quantization() const
{
	return m_editor->quantization();
}




void PianoRollWindow::play()
{
	m_editor->play();
}




void PianoRollWindow::stop()
{
	m_editor->stop();
}




void PianoRollWindow::record()
{
	stopStepRecording(); //step recording mode is mutually exclusive with other record modes

	m_editor->record();
}




void PianoRollWindow::recordAccompany()
{
	stopStepRecording(); //step recording mode is mutually exclusive with other record modes

	m_editor->recordAccompany();
}


void PianoRollWindow::toggleStepRecording()
{
	if(isRecording())
	{
		// step recording mode is mutually exclusive with other record modes
		// stop them before starting step recording
		stop();
	}

	m_editor->toggleStepRecording();

	updateStepRecordingIcon();
}

void PianoRollWindow::stopRecording()
{
	m_editor->stopRecording();
}




void PianoRollWindow::reset()
{
	m_editor->reset();
}




void PianoRollWindow::saveSettings( QDomDocument & doc, QDomElement & de )
{
	if( !m_editor->ghostNotes().empty() )
	{
		QDomElement ghostNotesRoot = doc.createElement( "ghostnotes" );
		for( Note *note : m_editor->ghostNotes() )
		{
			QDomElement ghostNoteNode = doc.createElement( "ghostnote" );
			ghostNoteNode.setAttribute( "len", note->length() );
			ghostNoteNode.setAttribute( "key", note->key() );
			ghostNoteNode.setAttribute( "pos", note->pos() );

			ghostNotesRoot.appendChild(ghostNoteNode);
		}
		de.appendChild( ghostNotesRoot );
	}

	if (m_editor->m_markedSemiTones.length() > 0)
	{
		QDomElement markedSemiTonesRoot = doc.createElement("markedSemiTones");
		for (int ix = 0; ix < m_editor->m_markedSemiTones.size(); ++ix)
		{
			QDomElement semiToneNode = doc.createElement("semiTone");
			semiToneNode.setAttribute("key", m_editor->m_markedSemiTones.at(ix));
			markedSemiTonesRoot.appendChild(semiToneNode);
		}
		de.appendChild(markedSemiTonesRoot);
	}

	MainWindow::saveWidgetState( this, de );
}




void PianoRollWindow::loadSettings( const QDomElement & de )
{
	m_editor->loadGhostNotes( de.firstChildElement("ghostnotes") );
	m_editor->loadMarkedSemiTones(de.firstChildElement("markedSemiTones"));

	MainWindow::restoreWidgetState( this, de );
}




QSize PianoRollWindow::sizeHint() const
{
	return { INITIAL_PIANOROLL_WIDTH, INITIAL_PIANOROLL_HEIGHT };
}



void PianoRollWindow::updateAfterPatternChange()
{
	patternRenamed();
	updateStepRecordingIcon(); //pattern change turn step recording OFF - update icon accordingly
}

void PianoRollWindow::patternRenamed()
{
	if ( currentPattern() )
	{
		setWindowTitle( tr( "Piano-Roll - %1" ).arg( currentPattern()->name() ) );
	}
	else
	{
		setWindowTitle( tr( "Piano-Roll - no pattern" ) );
	}
}




void PianoRollWindow::ghostPatternSet( bool state )
{
	m_clearGhostButton->setEnabled( state );
}




void PianoRollWindow::focusInEvent( QFocusEvent * event )
{
	// when the window is given focus, also give focus to the actual piano roll
	m_editor->setFocus( event->reason() );
}

void PianoRollWindow::stopStepRecording()
{
	if(m_editor->isStepRecording())
	{
		m_editor->toggleStepRecording();
		updateStepRecordingIcon();
	}
}

void PianoRollWindow::updateStepRecordingIcon()
{
	if(m_editor->isStepRecording())
	{
		m_toggleStepRecordingAction->setIcon(embed::getIconPixmap("record_step_on"));
	}
	else
	{
		m_toggleStepRecordingAction->setIcon(embed::getIconPixmap("record_step_off"));
	}
}
