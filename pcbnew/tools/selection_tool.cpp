/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2013-2017 CERN
 * @author Tomasz Wlostowski <tomasz.wlostowski@cern.ch>
 * @author Maciej Suminski <maciej.suminski@cern.ch>
 * Copyright (C) 2018-2020 KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include <limits>
#include <functional>
using namespace std::placeholders;
#include <class_board.h>
#include <class_board_item.h>
#include <class_track.h>
#include <class_module.h>
#include <pcb_shape.h>
#include <class_zone.h>
#include <collectors.h>
#include <confirm.h>
#include <dialog_find.h>
#include <dialog_filter_selection.h>
#include <class_draw_panel_gal.h>
#include <view/view_controls.h>
#include <preview_items/selection_area.h>
#include <painter.h>
#include <bitmaps.h>
#include <pcbnew_settings.h>
#include <tool/tool_event.h>
#include <tool/tool_manager.h>
#include <router/router_tool.h>
#include <connectivity/connectivity_data.h>
#include <footprint_viewer_frame.h>
#include <id.h>
#include "tool_event_utils.h"
#include "selection_tool.h"
#include "pcb_bright_box.h"
#include "pcb_actions.h"


class SELECT_MENU : public ACTION_MENU
{
public:
    SELECT_MENU() :
        ACTION_MENU( true )
    {
        SetTitle( _( "Select" ) );
        SetIcon( options_generic_xpm );

        Add( PCB_ACTIONS::filterSelection );

        AppendSeparator();

        Add( PCB_ACTIONS::selectConnection );
        Add( PCB_ACTIONS::selectNet );
        // This could be enabled if we have better logic for picking the target net with the mouse
        // Add( PCB_ACTIONS::deselectNet );
        Add( PCB_ACTIONS::selectSameSheet );
    }

private:
    ACTION_MENU* create() const override
    {
        return new SELECT_MENU();
    }
};


/**
 * Private implementation of firewalled private data
 */
class SELECTION_TOOL::PRIV
{
public:
    DIALOG_FILTER_SELECTION::OPTIONS m_filterOpts;
};


SELECTION_TOOL::SELECTION_TOOL() :
        PCB_TOOL_BASE( "pcbnew.InteractiveSelection" ),
        m_frame( NULL ),
        m_additive( false ),
        m_subtractive( false ),
        m_exclusive_or( false ),
        m_multiple( false ),
        m_skip_heuristics( false ),
        m_locked( true ),
        m_enteredGroup( nullptr ),
        m_priv( std::make_unique<PRIV>() )
{
    m_filter.lockedItems = false;
    m_filter.footprints  = true;
    m_filter.text        = true;
    m_filter.tracks      = true;
    m_filter.vias        = true;
    m_filter.pads        = true;
    m_filter.graphics    = true;
    m_filter.zones       = true;
    m_filter.keepouts    = true;
    m_filter.dimensions  = true;
    m_filter.otherItems  = true;
}


SELECTION_TOOL::~SELECTION_TOOL()
{
    getView()->Remove( &m_selection );
    getView()->Remove( &m_enteredGroupOverlay );
}


bool SELECTION_TOOL::Init()
{
    auto frame = getEditFrame<PCB_BASE_FRAME>();

    if( frame && ( frame->IsType( FRAME_FOOTPRINT_VIEWER )
                   || frame->IsType( FRAME_FOOTPRINT_VIEWER_MODAL ) ) )
    {
        frame->AddStandardSubMenus( m_menu );
        return true;
    }

    auto selectMenu = std::make_shared<SELECT_MENU>();
    selectMenu->SetTool( this );
    m_menu.AddSubMenu( selectMenu );

    auto& menu = m_menu.GetMenu();

    auto activeToolCondition =
            [ frame ] ( const SELECTION& aSel )
            {
                return !frame->ToolStackIsEmpty();
            };

    auto inGroupCondition =
            [this] ( const SELECTION& )
            {
                return m_enteredGroup != nullptr;
            };

    menu.AddMenu( selectMenu.get(), SELECTION_CONDITIONS::NotEmpty );
    menu.AddSeparator( 1000 );

    // "Cancel" goes at the top of the context menu when a tool is active
    menu.AddItem( ACTIONS::cancelInteractive, activeToolCondition, 1 );
    menu.AddItem( PCB_ACTIONS::groupLeave,    inGroupCondition,    1);
    menu.AddSeparator( 1 );

    if( frame )
        frame->AddStandardSubMenus( m_menu );

    return true;
}


void SELECTION_TOOL::Reset( RESET_REASON aReason )
{
    m_frame = getEditFrame<PCB_BASE_FRAME>();
    m_locked = true;

    if( m_enteredGroup )
        ExitGroup();

    if( aReason == TOOL_BASE::MODEL_RELOAD )
    {
        // Deselect any item being currently in edit, to avoid unexpected behavior
        // and remove pointers to the selected items from containers
        // without changing their properties (as they are already deleted
        // while a new board is loaded)
        ClearSelection( true );

        getView()->GetPainter()->GetSettings()->SetHighlight( false );
    }
    else
    {
        // Restore previous properties of selected items and remove them from containers
        ClearSelection( true );
    }

    // Reinsert the VIEW_GROUP, in case it was removed from the VIEW
    view()->Remove( &m_selection );
    view()->Add( &m_selection );

    view()->Remove( &m_enteredGroupOverlay );
    view()->Add( &m_enteredGroupOverlay );
}


int SELECTION_TOOL::Main( const TOOL_EVENT& aEvent )
{
    // Main loop: keep receiving events
    while( TOOL_EVENT* evt = Wait() )
    {
        bool dragAlwaysSelects = getEditFrame<PCB_BASE_FRAME>()->GetDragSelects();
        TRACK_DRAG_ACTION dragAction = getEditFrame<PCB_BASE_FRAME>()->Settings().m_TrackDragAction;
        m_additive = m_subtractive = m_exclusive_or = false;

        // OSX uses CTRL for context menu, and SHIFT is exclusive-or
#ifdef __WXOSX_MAC__
        if( evt->Modifier( MD_SHIFT ) )
            m_exclusive_or = true;
#else
        if( evt->Modifier( MD_SHIFT ) && evt->Modifier( MD_CTRL ) )
            m_subtractive = true;
        else if( evt->Modifier( MD_SHIFT ) )
            m_additive = true;
        else if( evt->Modifier( MD_CTRL ) )
            m_exclusive_or = true;
#endif

        bool modifier_enabled = m_subtractive || m_additive || m_exclusive_or;

        // Is the user requesting that the selection list include all possible
        // items without removing less likely selection candidates
        m_skip_heuristics = !!evt->Modifier( MD_ALT );

        // Single click? Select single object
        if( evt->IsClick( BUT_LEFT ) )
        {
            m_frame->FocusOnItem( nullptr );

            selectPoint( evt->Position() );
        }

        // Right click? if there is any object - show the context menu
        else if( evt->IsClick( BUT_RIGHT ) )
        {
            bool selectionCancelled = false;

            if( m_selection.Empty() )
            {
                selectPoint( evt->Position(), false, &selectionCancelled );
                m_selection.SetIsHover( true );
            }

            if( !selectionCancelled )
                m_menu.ShowContextMenu( m_selection );
        }

        // Double click? Display the properties window
        else if( evt->IsDblClick( BUT_LEFT ) )
        {
            m_frame->FocusOnItem( nullptr );

            if( m_selection.Empty() )
                selectPoint( evt->Position() );

            if( m_selection.GetSize() == 1 && m_selection[0]->Type() == PCB_GROUP_T )
            {
                EnterGroup();
            }
            else
            {
                m_toolMgr->RunAction( PCB_ACTIONS::properties, true );
            }
        }

        // Middle double click?  Do zoom to fit or zoom to objects
        else if( evt->IsDblClick( BUT_MIDDLE ) )
        {
            if( m_exclusive_or ) // Is CTRL key down?
                m_toolMgr->RunAction( ACTIONS::zoomFitObjects, true );
            else
                m_toolMgr->RunAction( ACTIONS::zoomFitScreen, true );
        }

        // Drag with LMB? Select multiple objects (or at least draw a selection box) or drag them
        else if( evt->IsDrag( BUT_LEFT ) )
        {
            m_frame->FocusOnItem( nullptr );
            m_toolMgr->ProcessEvent( EVENTS::InhibitSelectionEditing );

            if( modifier_enabled || dragAlwaysSelects )
            {
                selectMultiple();
            }
            else
            {
                // Selection is empty? try to start dragging the item under the point where drag
                // started
                if( m_selection.Empty() && selectCursor() )
                    m_selection.SetIsHover( true );

                // Check if dragging has started within any of selected items bounding box.
                // We verify "HasPosition()" first to protect against edge case involving
                // moving off menus that causes problems (issue #5250)
                if( evt->HasPosition() && selectionContains( evt->Position() ) )
                {
                    // Yes -> run the move tool and wait till it finishes
                    TRACK* track = dynamic_cast<TRACK*>( m_selection.GetItem( 0 ) );

                    if( track && dragAction == TRACK_DRAG_ACTION::DRAG )
                        m_toolMgr->RunAction( PCB_ACTIONS::drag45Degree, true );
                    else if( track && dragAction == TRACK_DRAG_ACTION::DRAG_FREE_ANGLE )
                        m_toolMgr->RunAction( PCB_ACTIONS::dragFreeAngle, true );
                    else
                        m_toolMgr->RunAction( PCB_ACTIONS::move, true );
                }
                else
                {
                    // No -> drag a selection box
                    selectMultiple();
                }
            }
        }

        else if( evt->IsCancel() )
        {
            m_frame->FocusOnItem( nullptr );

            if( m_enteredGroup )
                ExitGroup();

            ClearSelection();

            if( evt->FirstResponder() == this )
                m_toolMgr->RunAction( PCB_ACTIONS::clearHighlight );
        }

        else
            evt->SetPassEvent();


        if( m_frame->ToolStackIsEmpty() )
        {
            //move cursor prediction
            if( !modifier_enabled && !dragAlwaysSelects && !m_selection.Empty()
                    && evt->HasPosition() && selectionContains( evt->Position() ) )
                m_frame->GetCanvas()->SetCurrentCursor( KICURSOR::MOVING );
            else
            {
                if( m_additive )
                    m_frame->GetCanvas()->SetCurrentCursor( KICURSOR::ADD );
                else if( m_subtractive )
                    m_frame->GetCanvas()->SetCurrentCursor( KICURSOR::SUBTRACT );
                else if( m_exclusive_or )
                    m_frame->GetCanvas()->SetCurrentCursor( KICURSOR::XOR );
                else
                    m_frame->GetCanvas()->SetCurrentCursor( KICURSOR::ARROW );
            }
        }
    }

    return 0;
}


void SELECTION_TOOL::EnterGroup()
{
    wxCHECK_RET( m_selection.GetSize() == 1 && m_selection[0]->Type() == PCB_GROUP_T,
                 "EnterGroup called when selection is not a single group" );
    PCB_GROUP* aGroup = static_cast<PCB_GROUP*>( m_selection[0] );

    if( m_enteredGroup != NULL )
        ExitGroup();

    ClearSelection();
    m_enteredGroup = aGroup;
    m_enteredGroup->RunOnChildren( [&]( BOARD_ITEM* titem )
                                   {
                                       select( titem );
                                   } );

    m_enteredGroupOverlay.Add( m_enteredGroup );
}


void SELECTION_TOOL::ExitGroup( bool aSelectGroup )
{
    // Only continue if there is a group entered
    if( m_enteredGroup == nullptr )
        return;

    ClearSelection();

    if( aSelectGroup )
        select( m_enteredGroup );

    m_enteredGroupOverlay.Clear();
    m_enteredGroup = nullptr;
}


PCBNEW_SELECTION& SELECTION_TOOL::GetSelection()
{
    return m_selection;
}


PCBNEW_SELECTION& SELECTION_TOOL::RequestSelection( CLIENT_SELECTION_FILTER aClientFilter,
                                                    std::vector<BOARD_ITEM*>* aFiltered,
                                                    bool aConfirmLockedItems )
{
    bool selectionEmpty = m_selection.Empty();
    m_selection.SetIsHover( selectionEmpty );

    if( selectionEmpty )
    {
        m_toolMgr->RunAction( PCB_ACTIONS::selectionCursor, true, aClientFilter );
        m_selection.ClearReferencePoint();
    }

    if ( aConfirmLockedItems && CheckLock() == SELECTION_LOCKED )
    {
        ClearSelection();
        return m_selection;
    }

    if( aClientFilter )
    {
        enum DISPOSITION { BEFORE = 1, AFTER, BOTH };

        std::map<EDA_ITEM*, DISPOSITION> itemDispositions;
        GENERAL_COLLECTOR                collector;

        for( EDA_ITEM* item : m_selection )
        {
            collector.Append( item );
            itemDispositions[ item ] = BEFORE;
        }

        aClientFilter( VECTOR2I(), collector, this );

        for( EDA_ITEM* item : collector )
        {
            if( itemDispositions.count( item ) )
                itemDispositions[ item ] = BOTH;
            else
                itemDispositions[ item ] = AFTER;
        }

        // Unhighlight the BEFORE items before highlighting the AFTER items.
        // This is so that in the case of groups, if aClientFilter replaces a selection
        // with the enclosing group, the unhighlight of the element doesn't undo the
        // recursive highlighting of that elemetn by the group.

        for( std::pair<EDA_ITEM* const, DISPOSITION> itemDisposition : itemDispositions )
        {
            BOARD_ITEM* item = static_cast<BOARD_ITEM*>( itemDisposition.first );
            DISPOSITION disposition = itemDisposition.second;

            if( disposition == BEFORE )
            {
                if( aFiltered )
                    aFiltered->push_back( item );

                unhighlight( item, SELECTED, &m_selection );
            }
        }

        for( std::pair<EDA_ITEM* const, DISPOSITION> itemDisposition : itemDispositions )
        {
            BOARD_ITEM* item = static_cast<BOARD_ITEM*>( itemDisposition.first );
            DISPOSITION disposition = itemDisposition.second;

            if( disposition == AFTER )
            {
                highlight( item, SELECTED, &m_selection );
            }
            else if( disposition == BOTH )
            {
                // nothing to do
            }
        }

        m_frame->GetCanvas()->ForceRefresh();
    }

    return m_selection;
}


const GENERAL_COLLECTORS_GUIDE SELECTION_TOOL::getCollectorsGuide() const
{
    GENERAL_COLLECTORS_GUIDE guide( board()->GetVisibleLayers(),
                                    (PCB_LAYER_ID) view()->GetTopLayer(), view() );

    bool padsDisabled = !board()->IsElementVisible( LAYER_PADS );

    // account for the globals
    guide.SetIgnoreMTextsMarkedNoShow( ! board()->IsElementVisible( LAYER_MOD_TEXT_INVISIBLE ) );
    guide.SetIgnoreMTextsOnBack( ! board()->IsElementVisible( LAYER_MOD_TEXT_BK ) );
    guide.SetIgnoreMTextsOnFront( ! board()->IsElementVisible( LAYER_MOD_TEXT_FR ) );
    guide.SetIgnoreModulesOnBack( ! board()->IsElementVisible( LAYER_MOD_BK ) );
    guide.SetIgnoreModulesOnFront( ! board()->IsElementVisible( LAYER_MOD_FR ) );
    guide.SetIgnorePadsOnBack( padsDisabled || ! board()->IsElementVisible( LAYER_PAD_BK ) );
    guide.SetIgnorePadsOnFront( padsDisabled || ! board()->IsElementVisible( LAYER_PAD_FR ) );
    guide.SetIgnoreThroughHolePads( padsDisabled || ! board()->IsElementVisible( LAYER_PADS_TH ) );
    guide.SetIgnoreModulesVals( ! board()->IsElementVisible( LAYER_MOD_VALUES ) );
    guide.SetIgnoreModulesRefs( ! board()->IsElementVisible( LAYER_MOD_REFERENCES ) );
    guide.SetIgnoreThroughVias( ! board()->IsElementVisible( LAYER_VIAS ) );
    guide.SetIgnoreBlindBuriedVias( ! board()->IsElementVisible( LAYER_VIAS ) );
    guide.SetIgnoreMicroVias( ! board()->IsElementVisible( LAYER_VIAS ) );
    guide.SetIgnoreTracks( ! board()->IsElementVisible( LAYER_TRACKS ) );

    return guide;
}


bool SELECTION_TOOL::selectPoint( const VECTOR2I& aWhere, bool aOnDrag,
                                  bool* aSelectionCancelledFlag,
                                  CLIENT_SELECTION_FILTER aClientFilter )
{
    GENERAL_COLLECTORS_GUIDE   guide = getCollectorsGuide();
    GENERAL_COLLECTOR          collector;
    const PCB_DISPLAY_OPTIONS& displayOpts = m_frame->GetDisplayOptions();

    guide.SetIgnoreZoneFills( displayOpts.m_ZoneDisplayMode != ZONE_DISPLAY_MODE::SHOW_FILLED );

    if( m_enteredGroup &&
        !m_enteredGroup->GetBoundingBox().Contains( wxPoint( aWhere.x, aWhere.y ) ) )
    {
            ExitGroup();
    }

    collector.Collect( board(), m_editModules ? GENERAL_COLLECTOR::ModuleItems
                                              : GENERAL_COLLECTOR::AllBoardItems,
                       (wxPoint) aWhere, guide );

    // Remove unselectable items
    for( int i = collector.GetCount() - 1; i >= 0; --i )
    {
        if( !Selectable( collector[ i ] ) || ( aOnDrag && collector[i]->IsLocked() ) )
            collector.Remove( i );
    }

    m_selection.ClearReferencePoint();

    // Allow the client to do tool- or action-specific filtering to see if we
    // can get down to a single item
    if( aClientFilter )
        aClientFilter( aWhere, collector, this );

    // Apply the stateful filter
    FilterCollectedItems( collector );

    FilterCollectorForGroups( collector );

    // Apply some ugly heuristics to avoid disambiguation menus whenever possible
    if( collector.GetCount() > 1 && !m_skip_heuristics )
    {
        GuessSelectionCandidates( collector, aWhere );
    }

    // If still more than one item we're going to have to ask the user.
    if( collector.GetCount() > 1 )
    {
        if( aOnDrag )
            Wait( TOOL_EVENT( TC_ANY, TA_MOUSE_UP, BUT_LEFT ) );

        if( !doSelectionMenu( &collector, wxEmptyString ) )
        {
            if( aSelectionCancelledFlag )
                *aSelectionCancelledFlag = true;

            return false;
        }
    }

    bool anyAdded      = false;
    bool anySubtracted = false;

    if( !m_additive && !m_subtractive && !m_exclusive_or )
    {
        if( m_selection.GetSize() > 0 )
        {
            ClearSelection( true /*quiet mode*/ );
            anySubtracted = true;
        }
    }

    if( collector.GetCount() > 0 )
    {
        for( int i = 0; i < collector.GetCount(); ++i )
        {
            if( m_subtractive || ( m_exclusive_or && collector[i]->IsSelected() ) )
            {
                unselect( collector[i] );
                anySubtracted = true;
            }
            else
            {
                select( collector[i] );
                anyAdded = true;
            }
        }
    }

    if( anyAdded )
    {
        m_toolMgr->ProcessEvent( EVENTS::SelectedEvent );
        return true;
    }
    else if( anySubtracted )
    {
        m_toolMgr->ProcessEvent( EVENTS::UnselectedEvent );
        return true;
    }

    return false;
}


bool SELECTION_TOOL::selectCursor( bool aForceSelect, CLIENT_SELECTION_FILTER aClientFilter )
{
    if( aForceSelect || m_selection.Empty() )
    {
        ClearSelection( true /*quiet mode*/ );
        selectPoint( getViewControls()->GetCursorPosition( false ), false, NULL, aClientFilter );
    }

    return !m_selection.Empty();
}


bool SELECTION_TOOL::selectMultiple()
{
    bool cancelled = false;     // Was the tool cancelled while it was running?
    m_multiple = true;          // Multiple selection mode is active
    KIGFX::VIEW* view = getView();

    KIGFX::PREVIEW::SELECTION_AREA area;
    view->Add( &area );

    bool anyAdded = false;
    bool anySubtracted = false;

    while( TOOL_EVENT* evt = Wait() )
    {
        int width = area.GetEnd().x - area.GetOrigin().x;

        /* Selection mode depends on direction of drag-selection:
             * Left > Right : Select objects that are fully enclosed by selection
             * Right > Left : Select objects that are crossed by selection
             */
        bool windowSelection = width >= 0 ? true : false;

        if( view->IsMirroredX() )
            windowSelection = !windowSelection;

        m_frame->GetCanvas()->SetCurrentCursor(
                windowSelection ? KICURSOR::SELECT_WINDOW : KICURSOR::SELECT_LASSO );

        if( evt->IsCancelInteractive() || evt->IsActivate() )
        {
            cancelled = true;
            break;
        }

        if( evt->IsDrag( BUT_LEFT ) )
        {
            if( !m_additive && !m_subtractive && !m_exclusive_or )
            {
                if( m_selection.GetSize() > 0 )
                {
                    anySubtracted = true;
                    ClearSelection( true /*quiet mode*/ );
                }
            }

            // Start drawing a selection box
            area.SetOrigin( evt->DragOrigin() );
            area.SetEnd( evt->Position() );
            area.SetAdditive( m_additive );
            area.SetSubtractive( m_subtractive );
            area.SetExclusiveOr( m_exclusive_or );

            view->SetVisible( &area, true );
            view->Update( &area );
            getViewControls()->SetAutoPan( true );
        }

        if( evt->IsMouseUp( BUT_LEFT ) )
        {
            getViewControls()->SetAutoPan( false );

            // End drawing the selection box
            view->SetVisible( &area, false );

            std::vector<KIGFX::VIEW::LAYER_ITEM_PAIR> candidates;
            BOX2I selectionBox = area.ViewBBox();
            view->Query( selectionBox, candidates );    // Get the list of nearby items

            int height = area.GetEnd().y - area.GetOrigin().y;

            // Construct an EDA_RECT to determine BOARD_ITEM selection
            EDA_RECT selectionRect( (wxPoint) area.GetOrigin(), wxSize( width, height ) );

            selectionRect.Normalize();

            GENERAL_COLLECTOR collector;

            for( auto it = candidates.begin(), it_end = candidates.end(); it != it_end; ++it )
            {
                BOARD_ITEM* item = static_cast<BOARD_ITEM*>( it->first );

                if( item && Selectable( item ) && item->HitTest( selectionRect, windowSelection ) )
                    collector.Append( item );
            }

            // Apply the stateful filter
            FilterCollectedItems( collector );

            FilterCollectorForGroups( collector );

            for( EDA_ITEM* i : collector )
            {
                BOARD_ITEM* item = static_cast<BOARD_ITEM*>( i );

                if( m_subtractive || ( m_exclusive_or && item->IsSelected() ) )
                {
                    unselect( item );
                    anySubtracted = true;
                }
                else
                {
                    select( item );
                    anyAdded = true;
                }
            }

            m_selection.SetIsHover( false );

            // Inform other potentially interested tools
            if( anyAdded )
                m_toolMgr->ProcessEvent( EVENTS::SelectedEvent );
            else if( anySubtracted )
                m_toolMgr->ProcessEvent( EVENTS::UnselectedEvent );

            break;  // Stop waiting for events
        }
    }

    getViewControls()->SetAutoPan( false );

    // Stop drawing the selection box
    view->Remove( &area );
    m_multiple = false;         // Multiple selection mode is inactive

    if( !cancelled )
        m_selection.ClearReferencePoint();

    m_toolMgr->ProcessEvent( EVENTS::UninhibitSelectionEditing );

    return cancelled;
}


SELECTION_LOCK_FLAGS SELECTION_TOOL::CheckLock()
{
    if( !m_locked || m_editModules )
        return SELECTION_UNLOCKED;

    bool containsLocked = false;

    // Check if the selection contains locked items
    for( EDA_ITEM* item : m_selection )
    {
        switch( item->Type() )
        {
        case PCB_MODULE_T:
            if( static_cast<MODULE*>( item )->IsLocked() )
                containsLocked = true;
            break;

        case PCB_FP_SHAPE_T:
        case PCB_FP_TEXT_T:
        case PCB_FP_ZONE_AREA_T:
            if( static_cast<MODULE*>( item->GetParent() )->IsLocked() )
                containsLocked = true;
            break;

        default:    // suppress warnings
            break;
        }
    }

    if( containsLocked )
    {
        if( IsOK( m_frame, _( "Selection contains locked items. Do you want to continue?" ) ) )
        {
            m_locked = false;
            return SELECTION_LOCK_OVERRIDE;
        }
        else
            return SELECTION_LOCKED;
    }

    return SELECTION_UNLOCKED;
}


int SELECTION_TOOL::CursorSelection( const TOOL_EVENT& aEvent )
{
    CLIENT_SELECTION_FILTER aClientFilter = aEvent.Parameter<CLIENT_SELECTION_FILTER>();

    selectCursor( false, aClientFilter );

    return 0;
}


int SELECTION_TOOL::ClearSelection( const TOOL_EVENT& aEvent )
{
    ClearSelection();

    return 0;
}


int SELECTION_TOOL::SelectItems( const TOOL_EVENT& aEvent )
{
    std::vector<BOARD_ITEM*>* items = aEvent.Parameter<std::vector<BOARD_ITEM*>*>();

    if( items )
    {
        // Perform individual selection of each item before processing the event.
        for( BOARD_ITEM* item : *items )
            select( item );

        m_toolMgr->ProcessEvent( EVENTS::SelectedEvent );
    }

    return 0;
}


int SELECTION_TOOL::SelectItem( const TOOL_EVENT& aEvent )
{
    AddItemToSel( aEvent.Parameter<BOARD_ITEM*>() );
    return 0;
}


int SELECTION_TOOL::SelectAll( const TOOL_EVENT& aEvent )
{
    KIGFX::VIEW* view = getView();

    // hold all visible items
    std::vector<KIGFX::VIEW::LAYER_ITEM_PAIR> selectedItems;

    // Filter the view items based on the selection box
    BOX2I selectionBox;

    selectionBox.SetMaximum();
    view->Query( selectionBox, selectedItems );         // Get the list of selected items

    for( const KIGFX::VIEW::LAYER_ITEM_PAIR& item_pair : selectedItems )
    {
        BOARD_ITEM* item = static_cast<BOARD_ITEM*>( item_pair.first );

        if( !item || !Selectable( item ) || !itemPassesFilter( item ) )
            continue;

        select( item );
    }

    m_frame->GetCanvas()->ForceRefresh();

    return 0;
}


void SELECTION_TOOL::AddItemToSel( BOARD_ITEM* aItem, bool aQuietMode )
{
    if( aItem )
    {
        select( aItem );

        // Inform other potentially interested tools
        if( !aQuietMode )
            m_toolMgr->ProcessEvent( EVENTS::SelectedEvent );
    }
}


int SELECTION_TOOL::UnselectItems( const TOOL_EVENT& aEvent )
{
    std::vector<BOARD_ITEM*>* items = aEvent.Parameter<std::vector<BOARD_ITEM*>*>();

    if( items )
    {
        // Perform individual unselection of each item before processing the event
        for( auto item : *items )
            unselect( item );

        m_toolMgr->ProcessEvent( EVENTS::UnselectedEvent );
    }

    return 0;
}


int SELECTION_TOOL::UnselectItem( const TOOL_EVENT& aEvent )
{
    RemoveItemFromSel( aEvent.Parameter<BOARD_ITEM*>() );
    return 0;
}


void SELECTION_TOOL::RemoveItemFromSel( BOARD_ITEM* aItem, bool aQuietMode )
{
    if( aItem )
    {
        unselect( aItem );

        // Inform other potentially interested tools
        m_toolMgr->ProcessEvent( EVENTS::UnselectedEvent );
    }
}


void SELECTION_TOOL::BrightenItem( BOARD_ITEM* aItem )
{
    highlight( aItem, BRIGHTENED );
}


void SELECTION_TOOL::UnbrightenItem( BOARD_ITEM* aItem )
{
    unhighlight( aItem, BRIGHTENED );
}


void connectedItemFilter( const VECTOR2I&, GENERAL_COLLECTOR& aCollector, SELECTION_TOOL* sTool )
{
    // Narrow the collection down to a single BOARD_CONNECTED_ITEM for each represented net.
    // All other items types are removed.
    std::set<int> representedNets;

    for( int i = aCollector.GetCount() - 1; i >= 0; i-- )
    {
        BOARD_CONNECTED_ITEM* item = dynamic_cast<BOARD_CONNECTED_ITEM*>( aCollector[i] );
        if( !item )
            aCollector.Remove( i );
        else if ( representedNets.count( item->GetNetCode() ) )
            aCollector.Remove( i );
        else
            representedNets.insert( item->GetNetCode() );
    }
}


int SELECTION_TOOL::expandConnection( const TOOL_EVENT& aEvent )
{
    unsigned initialCount = 0;

    for( auto item : m_selection.GetItems() )
    {
        if( dynamic_cast<BOARD_CONNECTED_ITEM*>( item ) )
            initialCount++;
    }

    if( initialCount == 0 )
        selectCursor( true, connectedItemFilter );

    for( STOP_CONDITION stopCondition : { STOP_AT_JUNCTION, STOP_AT_PAD, STOP_NEVER } )
    {
        // copy the selection, since we're going to iterate and modify
        std::deque<EDA_ITEM*> selectedItems = m_selection.GetItems();

        for( EDA_ITEM* item : selectedItems )
            item->ClearTempFlags();

        for( EDA_ITEM* item : selectedItems )
        {
            TRACK* trackItem = dynamic_cast<TRACK*>( item );

            // Track items marked SKIP_STRUCT have already been visited
            if( trackItem && !( trackItem->GetFlags() & SKIP_STRUCT ) )
                selectConnectedTracks( *trackItem, stopCondition );
        }

        if( m_selection.GetItems().size() > initialCount )
            break;
    }

    // Inform other potentially interested tools
    if( m_selection.Size() > 0 )
        m_toolMgr->ProcessEvent( EVENTS::SelectedEvent );

    return 0;
}


void SELECTION_TOOL::selectConnectedTracks( BOARD_CONNECTED_ITEM& aStartItem,
                                            STOP_CONDITION aStopCondition )
{
    constexpr KICAD_T types[] = { PCB_TRACE_T, PCB_ARC_T, PCB_VIA_T, PCB_PAD_T, EOT };

    auto connectivity = board()->GetConnectivity();
    auto connectedItems = connectivity->GetConnectedItems( &aStartItem, types );

    std::map<wxPoint, std::vector<TRACK*>> trackMap;
    std::map<wxPoint, VIA*>                viaMap;
    std::map<wxPoint, D_PAD*>              padMap;

    // Build maps of connected items
    for( BOARD_CONNECTED_ITEM* item : connectedItems )
    {
        switch( item->Type() )
        {
        case PCB_ARC_T:
        case PCB_TRACE_T:
        {
            TRACK* track = static_cast<TRACK*>( item );
            trackMap[ track->GetStart() ].push_back( track );
            trackMap[ track->GetEnd() ].push_back( track );
        }
            break;

        case PCB_VIA_T:
        {
            VIA* via = static_cast<VIA*>( item );
            viaMap[ via->GetStart() ] = via;
        }
            break;

        case PCB_PAD_T:
        {
            D_PAD* pad = static_cast<D_PAD*>( item );
            padMap[ pad->GetPosition() ] = pad;
        }
            break;

        default:
            break;
        }

        item->SetState( SKIP_STRUCT, false );
    }

    std::vector<wxPoint> activePts;

    // Set up the initial active points
    switch( aStartItem.Type() )
    {
    case PCB_ARC_T:
    case PCB_TRACE_T:
        activePts.push_back( static_cast<TRACK*>( &aStartItem )->GetStart() );
        activePts.push_back( static_cast<TRACK*>( &aStartItem )->GetEnd() );
        break;

    case PCB_VIA_T:
        activePts.push_back( static_cast<TRACK*>( &aStartItem )->GetStart() );
        break;

    case PCB_PAD_T:
        activePts.push_back( aStartItem.GetPosition() );
        break;

    default:
        break;
    }

    bool expand = true;

    // Iterative push from all active points
    while( expand )
    {
        expand = false;

        for( int i = activePts.size() - 1; i >= 0; --i )
        {
            wxPoint pt = activePts[i];

            if( trackMap[ pt ].size() > 2 && aStopCondition == STOP_AT_JUNCTION )
            {
                activePts.erase( activePts.begin() + i );
                continue;
            }

            if( padMap.count( pt ) && aStopCondition != STOP_NEVER )
            {
                activePts.erase( activePts.begin() + i );
                continue;
            }

            for( TRACK* track : trackMap[ pt ] )
            {
                if( track->GetState( SKIP_STRUCT ) )
                    continue;

                track->SetState( SKIP_STRUCT, true );
                select( track );

                if( track->GetStart() == pt )
                    activePts.push_back( track->GetEnd() );
                else
                    activePts.push_back( track->GetStart() );

                expand = true;
            }

            if( viaMap.count( pt ) && !viaMap[ pt ]->IsSelected() )
                select( viaMap[ pt ] );

            activePts.erase( activePts.begin() + i );
        }
    }
}


void SELECTION_TOOL::selectAllItemsOnNet( int aNetCode, bool aSelect )
{
    constexpr KICAD_T types[] = { PCB_TRACE_T, PCB_ARC_T, PCB_VIA_T, EOT };
    auto connectivity = board()->GetConnectivity();

    for( BOARD_CONNECTED_ITEM* item : connectivity->GetNetItems( aNetCode, types ) )
        if( itemPassesFilter( item ) )
            aSelect ? select( item ) : unselect( item );
}


int SELECTION_TOOL::selectNet( const TOOL_EVENT& aEvent )
{
    bool select = aEvent.IsAction( &PCB_ACTIONS::selectNet );

    // If we've been passed an argument, just select that netcode1
    int netcode = aEvent.Parameter<intptr_t>();

    if( netcode > 0 )
    {
        selectAllItemsOnNet( netcode, select );
        return 0;
    }

    if( !selectCursor() )
        return 0;

    // copy the selection, since we're going to iterate and modify
    auto selection = m_selection.GetItems();

    for( EDA_ITEM* i : selection )
    {
        BOARD_CONNECTED_ITEM* connItem = dynamic_cast<BOARD_CONNECTED_ITEM*>( i );

        if( connItem )
            selectAllItemsOnNet( connItem->GetNetCode(), select );
    }

    // Inform other potentially interested tools
    if( m_selection.Size() > 0 )
        m_toolMgr->ProcessEvent( EVENTS::SelectedEvent );

    return 0;
}


void SELECTION_TOOL::selectAllItemsOnSheet( wxString& aSheetPath )
{
    std::list<MODULE*> modList;

    // store all footprints that are on that sheet path
    for( MODULE* module : board()->Modules() )
    {
        if( module == nullptr )
            continue;

        wxString footprint_path = module->GetPath().AsString().BeforeLast('/');

        if( aSheetPath.IsEmpty() )
            aSheetPath += '/';

        if( footprint_path == aSheetPath )
            modList.push_back( module );
    }

    //Generate a list of all pads, and of all nets they belong to.
    std::list<int> netcodeList;
    std::list<D_PAD*> padList;
    for( MODULE* mmod : modList )
    {
        for( D_PAD* pad : mmod->Pads() )
        {
            if( pad->IsConnected() )
            {
                netcodeList.push_back( pad->GetNetCode() );
                padList.push_back( pad );
            }
        }
    }
    // remove all duplicates
    netcodeList.sort();
    netcodeList.unique();

    // auto select trivial connections segments which are launched from the pads
    std::list<TRACK*> launchTracks;

    for( D_PAD* pad : padList )
        selectConnectedTracks( *pad, STOP_NEVER );

    // now we need to find all footprints that are connected to each of these nets
    // then we need to determine if these modules are in the list of footprints
    // belonging to this sheet ( modList )
    std::list<int> removeCodeList;
    constexpr KICAD_T padType[] = { PCB_PAD_T, EOT };

    for( int netCode : netcodeList )
    {
        for( BOARD_CONNECTED_ITEM* mitem : board()->GetConnectivity()->GetNetItems( netCode, padType ) )
        {
            if( mitem->Type() == PCB_PAD_T && !alg::contains( modList, mitem->GetParent() ) )
            {
                // if we cannot find the module of the pad in the modList
                // then we can assume that that module is not located in the same
                // schematic, therefore invalidate this netcode.
                removeCodeList.push_back( netCode );
                break;
            }
        }
    }

    // remove all duplicates
    removeCodeList.sort();
    removeCodeList.unique();

    for( int removeCode : removeCodeList )
    {
        netcodeList.remove( removeCode );
    }

    std::list<BOARD_CONNECTED_ITEM*> localConnectionList;
    constexpr KICAD_T trackViaType[] = { PCB_TRACE_T, PCB_ARC_T, PCB_VIA_T, EOT };

    for( int netCode : netcodeList )
    {
        for( BOARD_CONNECTED_ITEM* item : board()->GetConnectivity()->GetNetItems( netCode, trackViaType ) )
            localConnectionList.push_back( item );
    }

    for( BOARD_ITEM* i : modList )
    {
        if( i != NULL )
            select( i );
    }

    for( BOARD_CONNECTED_ITEM* i : localConnectionList )
    {
        if( i != NULL )
            select( i );
    }
}


void SELECTION_TOOL::zoomFitSelection()
{
    //Should recalculate the view to zoom in on the selection
    auto selectionBox = m_selection.GetBoundingBox();
    auto view = getView();

    VECTOR2D screenSize = view->ToWorld( m_frame->GetCanvas()->GetClientSize(), false );
    screenSize.x = std::max( 10.0, screenSize.x );
    screenSize.y = std::max( 10.0, screenSize.y );

    if( selectionBox.GetWidth() != 0  || selectionBox.GetHeight() != 0 )
    {
        VECTOR2D vsize = selectionBox.GetSize();
        double scale = view->GetScale() / std::max( fabs( vsize.x / screenSize.x ),
                fabs( vsize.y / screenSize.y ) );
        view->SetScale( scale );
        view->SetCenter( selectionBox.Centre() );
        view->Add( &m_selection );
    }

    m_frame->GetCanvas()->ForceRefresh();
}


int SELECTION_TOOL::selectSheetContents( const TOOL_EVENT& aEvent )
{
    ClearSelection( true /*quiet mode*/ );
    wxString sheetPath = *aEvent.Parameter<wxString*>();

    selectAllItemsOnSheet( sheetPath );

    zoomFitSelection();

    if( m_selection.Size() > 0 )
        m_toolMgr->ProcessEvent( EVENTS::SelectedEvent );

    return 0;
}


int SELECTION_TOOL::selectSameSheet( const TOOL_EVENT& aEvent )
{
    if( !selectCursor( true ) )
        return 0;

    // this function currently only supports footprints since they are only
    // on one sheet.
    auto item = m_selection.Front();

    if( !item )
        return 0;

    if( item->Type() != PCB_MODULE_T )
        return 0;

    auto mod = dynamic_cast<MODULE*>( item );

    if( mod->GetPath().empty() )
        return 0;

    ClearSelection( true /*quiet mode*/ );

    // get the sheet path only.
    wxString sheetPath = mod->GetPath().AsString().BeforeLast( '/' );

    if( sheetPath.IsEmpty() )
        sheetPath += '/';

    selectAllItemsOnSheet( sheetPath );

    // Inform other potentially interested tools
    if( m_selection.Size() > 0 )
        m_toolMgr->ProcessEvent( EVENTS::SelectedEvent );

    return 0;
}


void SELECTION_TOOL::findCallback( BOARD_ITEM* aItem )
{
    bool cleared = false;

    if( m_selection.GetSize() > 0 )
    {
        // Don't fire an event now; most of the time it will be redundant as we're about to
        // fire a SelectedEvent.
        cleared = true;
        ClearSelection( true /*quiet mode*/ );
    }

    if( aItem )
    {
        select( aItem );
        m_frame->FocusOnLocation( aItem->GetPosition() );

        // Inform other potentially interested tools
        m_toolMgr->ProcessEvent( EVENTS::SelectedEvent );
    }
    else if( cleared )
    {
        m_toolMgr->ProcessEvent( EVENTS::ClearedEvent );
    }

    m_frame->GetCanvas()->ForceRefresh();
}


int SELECTION_TOOL::find( const TOOL_EVENT& aEvent )
{
    DIALOG_FIND dlg( m_frame );
    dlg.SetCallback( std::bind( &SELECTION_TOOL::findCallback, this, _1 ) );
    dlg.ShowModal();

    return 0;
}


/**
 * Function itemIsIncludedByFilter()
 *
 * Determine if an item is included by the filter specified
 *
 * @return true if aItem should be selected by this filter (i..e not filtered out)
 */
static bool itemIsIncludedByFilter( const BOARD_ITEM& aItem, const BOARD& aBoard,
                                    const DIALOG_FILTER_SELECTION::OPTIONS& aFilterOptions )
{
    bool include = true;
    const PCB_LAYER_ID layer = aItem.GetLayer();

    // if the item needs to be checked against the options
    if( include )
    {
        switch( aItem.Type() )
        {
        case PCB_MODULE_T:
        {
            const auto& module = static_cast<const MODULE&>( aItem );

            include = aFilterOptions.includeModules;

            if( include && !aFilterOptions.includeLockedModules )
            {
                include = !module.IsLocked();
            }

            break;
        }
        case PCB_TRACE_T:
        case PCB_ARC_T:
        {
            include = aFilterOptions.includeTracks;
            break;
        }
        case PCB_VIA_T:
        {
            include = aFilterOptions.includeVias;
            break;
        }
        case PCB_ZONE_AREA_T:
        {
            include = aFilterOptions.includeZones;
            break;
        }
        case PCB_SHAPE_T:
        case PCB_TARGET_T:
        case PCB_DIM_ALIGNED_T:
        case PCB_DIM_CENTER_T:
        case PCB_DIM_ORTHOGONAL_T:
        case PCB_DIM_LEADER_T:
        {
            if( layer == Edge_Cuts )
                include = aFilterOptions.includeBoardOutlineLayer;
            else
                include = aFilterOptions.includeItemsOnTechLayers;
            break;
        }
        case PCB_TEXT_T:
        {
            include = aFilterOptions.includePcbTexts;
            break;
        }
        default:
        {
            // no filtering, just select it
            break;
        }
        }
    }

    return include;
}


int SELECTION_TOOL::filterSelection( const TOOL_EVENT& aEvent )
{
    const BOARD&                      board = *getModel<BOARD>();
    DIALOG_FILTER_SELECTION::OPTIONS& opts = m_priv->m_filterOpts;
    DIALOG_FILTER_SELECTION           dlg( m_frame, opts );

    const int cmd = dlg.ShowModal();

    if( cmd != wxID_OK )
        return 0;

    // copy current selection
    std::deque<EDA_ITEM*> selection = m_selection.GetItems();

    ClearSelection( true /*quiet mode*/ );

    // re-select items from the saved selection according to the dialog options
    for( EDA_ITEM* i : selection )
    {
        BOARD_ITEM* item = static_cast<BOARD_ITEM*>( i );
        bool        include = itemIsIncludedByFilter( *item, board, opts );

        if( include )
            select( item );
    }

    m_toolMgr->ProcessEvent( EVENTS::SelectedEvent );

    return 0;
}


void SELECTION_TOOL::FilterCollectedItems( GENERAL_COLLECTOR& aCollector )
{
    if( aCollector.GetCount() == 0 )
        return;

    std::set<BOARD_ITEM*> rejected;

    for( EDA_ITEM* i : aCollector )
    {
        BOARD_ITEM* item = static_cast<BOARD_ITEM*>( i );

        if( !itemPassesFilter( item ) )
            rejected.insert( item );
    }

    for( BOARD_ITEM* item : rejected )
        aCollector.Remove( item );
}


bool SELECTION_TOOL::itemPassesFilter( BOARD_ITEM* aItem )
{
    if( aItem->IsLocked() && !m_filter.lockedItems )
        return false;

    switch( aItem->Type() )
    {
    case PCB_MODULE_T:
        if( !m_filter.footprints )
            return false;

        break;

    case PCB_PAD_T:
    {
        if( !m_filter.pads )
            return false;

        break;
    }

    case PCB_TRACE_T:
    case PCB_ARC_T:
        if( !m_filter.tracks )
            return false;

        break;

    case PCB_VIA_T:
        if( !m_filter.vias )
            return false;

        break;

    case PCB_ZONE_AREA_T:
    {
        ZONE_CONTAINER* zone = static_cast<ZONE_CONTAINER*>( aItem );

        if( ( !m_filter.zones && !zone->GetIsRuleArea() )
            || ( !m_filter.keepouts && zone->GetIsRuleArea() ) )
        {
            return false;
        }

        break;
    }
    case PCB_SHAPE_T:
    case PCB_TARGET_T:
        if( !m_filter.graphics )
            return false;

        break;

    case PCB_FP_TEXT_T:
    case PCB_TEXT_T:
        if( !m_filter.text )
            return false;

        break;

    case PCB_DIM_ALIGNED_T:
    case PCB_DIM_CENTER_T:
    case PCB_DIM_ORTHOGONAL_T:
    case PCB_DIM_LEADER_T:
        if( !m_filter.dimensions )
            return false;

        break;

    default:
        if( !m_filter.otherItems )
            return false;
    }

    return true;
}


void SELECTION_TOOL::ClearSelection( bool aQuietMode )
{
    if( m_selection.Empty() )
        return;

    while( m_selection.GetSize() )
        unhighlight( static_cast<BOARD_ITEM*>( m_selection.Front() ), SELECTED, &m_selection );

    view()->Update( &m_selection );

    m_selection.SetIsHover( false );
    m_selection.ClearReferencePoint();

    m_locked = true;

    // Inform other potentially interested tools
    if( !aQuietMode )
    {
        m_toolMgr->ProcessEvent( EVENTS::ClearedEvent );
        m_toolMgr->RunAction( PCB_ACTIONS::hideDynamicRatsnest, true );
    }
}


void SELECTION_TOOL::RebuildSelection()
{
    m_selection.Clear();

    INSPECTOR_FUNC inspector = [&] ( EDA_ITEM* item, void* testData )
    {
        if( item->IsSelected() )
        {
            EDA_ITEM* parent = item->GetParent();

            // Flags on module children might be set only because the parent is selected.
            if( parent && parent->Type() == PCB_MODULE_T && parent->IsSelected() )
                return SEARCH_RESULT::CONTINUE;

            highlight( (BOARD_ITEM*) item, SELECTED, &m_selection );
        }

        return SEARCH_RESULT::CONTINUE;
    };

    board()->Visit( inspector, nullptr,  m_editModules ? GENERAL_COLLECTOR::ModuleItems
                                                       : GENERAL_COLLECTOR::AllBoardItems );
}


int SELECTION_TOOL::SelectionMenu( const TOOL_EVENT& aEvent )
{
    GENERAL_COLLECTOR* collector = aEvent.Parameter<GENERAL_COLLECTOR*>();

    doSelectionMenu( collector, wxEmptyString );

    return 0;
}


bool SELECTION_TOOL::doSelectionMenu( GENERAL_COLLECTOR* aCollector, const wxString& aTitle )
{
    BOARD_ITEM*      current = nullptr;
    PCBNEW_SELECTION highlightGroup;
    bool             selectAll = false;
    bool             expandSelection = false;

    highlightGroup.SetLayer( LAYER_SELECT_OVERLAY );
    getView()->Add( &highlightGroup );

    do
    {
        /// The user has requested the full, non-limited list of selection items
        if( expandSelection )
            aCollector->Combine();

        expandSelection = false;

        int         limit = std::min( 9, aCollector->GetCount() );
        ACTION_MENU menu( true );

        for( int i = 0; i < limit; ++i )
        {
            wxString    text;
            BOARD_ITEM* item = ( *aCollector )[i];
            text             = item->GetSelectMenuText( m_frame->GetUserUnits() );

            wxString menuText = wxString::Format( "&%d. %s\t%d", i + 1, text, i + 1 );
            menu.Add( menuText, i + 1, item->GetMenuImage() );
        }

        menu.AppendSeparator();
        menu.Add( _( "Select &All\tA" ), limit + 1, plus_xpm );

        if( !expandSelection && aCollector->HasAdditionalItems() )
            menu.Add( _( "&Expand Selection\tE" ), limit + 2, nullptr );

        if( aTitle.Length() )
        {
            menu.SetTitle( aTitle );
            menu.SetIcon( info_xpm );
            menu.DisplayTitle( true );
        }
        else
            menu.DisplayTitle( false );

        SetContextMenu( &menu, CMENU_NOW );

        while( TOOL_EVENT* evt = Wait() )
        {
            if( evt->Action() == TA_CHOICE_MENU_UPDATE )
            {
                if( selectAll )
                {
                    for( int i = 0; i < aCollector->GetCount(); ++i )
                        unhighlight( ( *aCollector )[i], BRIGHTENED, &highlightGroup );
                }
                else if( current )
                    unhighlight( current, BRIGHTENED, &highlightGroup );

                int id = *evt->GetCommandId();

                // User has pointed an item, so show it in a different way
                if( id > 0 && id <= limit )
                {
                    current = ( *aCollector )[id - 1];
                    highlight( current, BRIGHTENED, &highlightGroup );
                }
                else
                    current = nullptr;

                // User has pointed on the "Select All" option
                if( id == limit + 1 )
                {
                    for( int i = 0; i < aCollector->GetCount(); ++i )
                        highlight( ( *aCollector )[i], BRIGHTENED, &highlightGroup );
                    selectAll = true;
                }
                else
                    selectAll = false;
            }
            else if( evt->Action() == TA_CHOICE_MENU_CHOICE )
            {
                if( selectAll )
                {
                    for( int i = 0; i < aCollector->GetCount(); ++i )
                        unhighlight( ( *aCollector )[i], BRIGHTENED, &highlightGroup );
                }
                else if( current )
                    unhighlight( current, BRIGHTENED, &highlightGroup );

                OPT<int> id = evt->GetCommandId();

                // User has selected the "Select All" option
                if( id == limit + 1 )
                {
                    selectAll = true;
                    current   = nullptr;
                }
                else if( id == limit + 2 )
                {
                    expandSelection = true;
                    selectAll       = false;
                    current         = nullptr;
                }
                // User has selected an item, so this one will be returned
                else if( id && ( *id > 0 ) && ( *id <= limit ) )
                {
                    selectAll = false;
                    current   = ( *aCollector )[*id - 1];
                }
                else
                {
                    selectAll = false;
                    current   = nullptr;
                }
            }
            else if( evt->Action() == TA_CHOICE_MENU_CLOSED )
            {
                break;
            }
        }
    } while( expandSelection );

    getView()->Remove( &highlightGroup );

    if( selectAll )
        return true;
    else if( current )
    {
        aCollector->Empty();
        aCollector->Append( current );
        return true;
    }

    return false;
}


BOARD_ITEM* SELECTION_TOOL::pickSmallestComponent( GENERAL_COLLECTOR* aCollector )
{
    int count = aCollector->GetPrimaryCount();     // try to use preferred layer

    if( 0 == count )
        count = aCollector->GetCount();

    for( int i = 0; i < count; ++i )
    {
        if( ( *aCollector )[i]->Type() != PCB_MODULE_T )
            return NULL;
    }

    // All are footprints, now find smallest MODULE
    int minDim = 0x7FFFFFFF;
    int minNdx = 0;

    for( int i = 0; i < count; ++i )
    {
        MODULE* module = (MODULE*) ( *aCollector )[i];

        int lx = module->GetFootprintRect().GetWidth();
        int ly = module->GetFootprintRect().GetHeight();

        int lmin = std::min( lx, ly );

        if( lmin < minDim )
        {
            minDim = lmin;
            minNdx = i;
        }
    }

    return (*aCollector)[minNdx];
}


bool SELECTION_TOOL::Selectable( const BOARD_ITEM* aItem, bool checkVisibilityOnly ) const
{
    const RENDER_SETTINGS* settings = getView()->GetPainter()->GetSettings();

    if( settings->GetHighContrast() )
    {
        std::set<unsigned int> activeLayers = settings->GetHighContrastLayers();
        bool                   onActiveLayer = false;

        for( unsigned int layer : activeLayers )
        {
            // NOTE: Only checking the regular layers (not GAL meta-layers)
            if( layer < PCB_LAYER_ID_COUNT && aItem->IsOnLayer( ToLAYER_ID( layer ) ) )
            {
                onActiveLayer = true;
                break;
            }
        }

        if( !onActiveLayer ) // We do not want to select items that are in the background
            return false;
    }

    switch( aItem->Type() )
    {
    case PCB_ZONE_AREA_T:
    case PCB_FP_ZONE_AREA_T:
    {
        const ZONE_CONTAINER* zone = static_cast<const ZONE_CONTAINER*>( aItem );

        // Check to see if this keepout is part of a footprint
        // If it is, and we are not editing the footprint, it should not be selectable
        bool zoneInFootprint = zone->GetParent() && zone->GetParent()->Type() == PCB_MODULE_T;

        if( zoneInFootprint && !m_editModules && !checkVisibilityOnly )
            return false;

        // zones can exist on multiple layers!
        return ( zone->GetLayerSet() & board()->GetVisibleLayers() ).any();
    }
        break;

    case PCB_TRACE_T:
    case PCB_ARC_T:
        if( !board()->IsElementVisible( LAYER_TRACKS ) )
            return false;
        break;

    case PCB_VIA_T:
    {
        if( !board()->IsElementVisible( LAYER_VIAS ) )
            return false;

        const VIA* via = static_cast<const VIA*>( aItem );

        // For vias it is enough if only one of its layers is visible
        return ( board()->GetVisibleLayers() & via->GetLayerSet() ).any();
    }

    case PCB_MODULE_T:
    {
        // In modedit, we do not want to select the module itself.
        if( m_editModules )
            return false;

        // Allow selection of footprints if some part of the footprint is visible.

        MODULE* module = const_cast<MODULE*>( static_cast<const MODULE*>( aItem ) );

        for( BOARD_ITEM* item : module->GraphicalItems() )
        {
            if( Selectable( item, true ) )
                return true;
        }

        for( D_PAD* pad : module->Pads() )
        {
            if( Selectable( pad, true ) )
                return true;
        }

        for( ZONE_CONTAINER* zone : module->Zones() )
        {
            if( Selectable( zone, true ) )
                return true;
        }

        return false;
    }

    case PCB_FP_TEXT_T:
        // Multiple selection is only allowed in modedit mode.  In pcbnew, you have to select
        // module subparts one by one, rather than with a drag selection.  This is so you can
        // pick up items under an (unlocked) module without also moving the module's sub-parts.
        if( !m_editModules && !checkVisibilityOnly )
        {
            if( m_multiple && !settings->GetHighContrast() )
                return false;
        }

        if( !m_editModules && !view()->IsVisible( aItem ) )
            return false;

        break;

    case PCB_FP_SHAPE_T:
        // Module edge selections are only allowed in modedit mode.
        if( !m_editModules && !checkVisibilityOnly )
            return false;

        break;

    case PCB_PAD_T:
    {
        // Multiple selection is only allowed in modedit mode.  In pcbnew, you have to select
        // module subparts one by one, rather than with a drag selection.  This is so you can
        // pick up items under an (unlocked) module without also moving the module's sub-parts.
        if( !m_editModules && !checkVisibilityOnly )
        {
            if( m_multiple )
                return false;
        }

        if( aItem->Type() == PCB_PAD_T )
        {
            auto pad = static_cast<const D_PAD*>( aItem );

            // Check render mode (from the Items tab) first
            switch( pad->GetAttribute() )
            {
            case PAD_ATTRIB_PTH:
            case PAD_ATTRIB_NPTH:
                if( !board()->IsElementVisible( LAYER_PADS_TH ) )
                    return false;
                break;

            case PAD_ATTRIB_CONN:
            case PAD_ATTRIB_SMD:
                if( pad->IsOnLayer( F_Cu ) && !board()->IsElementVisible( LAYER_PAD_FR ) )
                    return false;
                else if( pad->IsOnLayer( B_Cu ) && !board()->IsElementVisible( LAYER_PAD_BK ) )
                    return false;
                break;
            }

            // Otherwise, pads are selectable if any draw layer is visible
            return ( pad->GetLayerSet() & board()->GetVisibleLayers() ).any();
        }

        break;
    }

    case PCB_GROUP_T:
    {
        PCB_GROUP* group = const_cast<PCB_GROUP*>( static_cast<const PCB_GROUP*>( aItem ) );

        // Similar to logic for module, a group is selectable if any of its
        // members are. (This recurses.)
        for( BOARD_ITEM* item : group->GetItems() )
        {
            if( Selectable( item, true ) )
                return true;
        }

        return false;
    }

    case PCB_MARKER_T:  // Always selectable
        return true;

    // These are not selectable
    case PCB_NETINFO_T:
    case NOT_USED:
    case TYPE_NOT_INIT:
        return false;

    default:    // Suppress warnings
        break;
    }

    // All other items are selected only if the layer on which they exist is visible
    return board()->IsLayerVisible( aItem->GetLayer() )
            && aItem->ViewGetLOD( aItem->GetLayer(), view() ) < view()->GetScale();
}


void SELECTION_TOOL::select( BOARD_ITEM* aItem )
{
    if( aItem->IsSelected() )
    {
        return;
    }

    if( aItem->Type() == PCB_PAD_T )
    {
        MODULE* module = static_cast<MODULE*>( aItem->GetParent() );

        if( m_selection.Contains( module ) )
            return;
    }

    highlight( aItem, SELECTED, &m_selection );
}


void SELECTION_TOOL::unselect( BOARD_ITEM* aItem )
{
    unhighlight( aItem, SELECTED, &m_selection );

    if( m_selection.Empty() )
        m_locked = true;
}


void SELECTION_TOOL::highlight( BOARD_ITEM* aItem, int aMode, PCBNEW_SELECTION* aGroup )
{
    highlightInternal( aItem, aMode, aGroup, false );

    view()->Update( aItem );

    // Many selections are very temporal and updating the display each time just
    // creates noise.
    if( aMode == BRIGHTENED )
        getView()->MarkTargetDirty( KIGFX::TARGET_OVERLAY );
}


void SELECTION_TOOL::highlightInternal( BOARD_ITEM* aItem, int aMode,
                                        PCBNEW_SELECTION* aSelectionViewGroup, bool isChild )
{
    wxLogTrace( "GRP", wxString::Format( "highlight() %s",
                                         aItem->GetSelectMenuText( EDA_UNITS::MILLIMETRES ) ) );

    if( aMode == SELECTED )
        aItem->SetSelected();
    else if( aMode == BRIGHTENED )
        aItem->SetBrightened();

    if( aSelectionViewGroup )
    {
        // Hide the original item, so it is shown only on overlay
        view()->Hide( aItem, true );

        if( !isChild || aMode == BRIGHTENED )
            aSelectionViewGroup->Add( aItem );
    }

    // footprints are treated in a special way - when they are highlighted, we have to highlight
    // all the parts that make the module, not the module itself
    if( aItem->Type() == PCB_MODULE_T )
    {
        static_cast<MODULE*>( aItem )->RunOnChildren(
                [&]( BOARD_ITEM* aChild )
                {
                    highlightInternal( aChild, aMode, aSelectionViewGroup, true );
                } );
    }
    else if( aItem->Type() == PCB_GROUP_T )
    {
        static_cast<PCB_GROUP*>( aItem )->RunOnChildren(
                [&]( BOARD_ITEM* aChild )
                {
                    highlightInternal( aChild, aMode, aSelectionViewGroup, true );
                } );
    }
}


void SELECTION_TOOL::unhighlight( BOARD_ITEM* aItem, int aMode, PCBNEW_SELECTION* aGroup )
{
    unhighlightInternal( aItem, aMode, aGroup, false );

    view()->Update( aItem );

    // Many selections are very temporal and updating the display each time just
    // creates noise.
    if( aMode == BRIGHTENED )
        getView()->MarkTargetDirty( KIGFX::TARGET_OVERLAY );
}


void SELECTION_TOOL::unhighlightInternal( BOARD_ITEM* aItem, int aMode,
                                          PCBNEW_SELECTION* aSelectionViewGroup, bool isChild )
{
    wxLogTrace( "GRP", wxString::Format( "unhighlight() %s",
                                         aItem->GetSelectMenuText( EDA_UNITS::MILLIMETRES ) ) );

    if( aMode == SELECTED )
        aItem->ClearSelected();
    else if( aMode == BRIGHTENED )
        aItem->ClearBrightened();

    if( aSelectionViewGroup )
    {
        aSelectionViewGroup->Remove( aItem );

        // Restore original item visibility
        view()->Hide( aItem, false );

        // N.B. if we clear the selection flag for sub-elements, we need to also
        // remove the element from the selection group (if it exists)
        if( isChild )
            view()->Update( aItem );
    }

    // footprints are treated in a special way - when they are highlighted, we have to
    // highlight all the parts that make the module, not the module itself
    if( aItem->Type() == PCB_MODULE_T )
    {
        static_cast<MODULE*>( aItem )->RunOnChildren(
                [&]( BOARD_ITEM* aChild )
                {
                    unhighlightInternal( aChild, aMode, aSelectionViewGroup, true );
                } );
    }
    else if( aItem->Type() == PCB_GROUP_T )
    {
        static_cast<PCB_GROUP*>( aItem )->RunOnChildren(
                [&]( BOARD_ITEM* aChild )
                {
                    unhighlightInternal( aChild, aMode, aSelectionViewGroup, true );
                } );
    }
}


bool SELECTION_TOOL::selectionContains( const VECTOR2I& aPoint ) const
{
    const unsigned GRIP_MARGIN = 20;
    VECTOR2I margin = getView()->ToWorld( VECTOR2I( GRIP_MARGIN, GRIP_MARGIN ), false );

    // Check if the point is located within any of the currently selected items bounding boxes
    for( auto item : m_selection )
    {
        BOX2I itemBox = item->ViewBBox();
        itemBox.Inflate( margin.x, margin.y );    // Give some margin for gripping an item

        if( itemBox.Contains( aPoint ) )
            return true;
    }

    return false;
}


static EDA_RECT getRect( const BOARD_ITEM* aItem )
{
    if( aItem->Type() == PCB_MODULE_T )
        return static_cast<const MODULE*>( aItem )->GetFootprintRect();

    return aItem->GetBoundingBox();
}


static double calcArea( const BOARD_ITEM* aItem )
{
    if( aItem->Type() == PCB_TRACE_T )
    {
        const TRACK* t = static_cast<const TRACK*>( aItem );
        return ( t->GetWidth() + t->GetLength() ) * t->GetWidth();
    }

    return getRect( aItem ).GetArea();
}


/*static double calcMinArea( GENERAL_COLLECTOR& aCollector, KICAD_T aType )
{
    double best = std::numeric_limits<double>::max();

    if( !aCollector.GetCount() )
        return 0.0;

    for( int i = 0; i < aCollector.GetCount(); i++ )
    {
        BOARD_ITEM* item = aCollector[i];
        if( item->Type() == aType )
            best = std::min( best, calcArea( item ) );
    }

    return best;
}*/


static double calcMaxArea( GENERAL_COLLECTOR& aCollector, KICAD_T aType )
{
    double best = 0.0;

    for( int i = 0; i < aCollector.GetCount(); i++ )
    {
        BOARD_ITEM* item = aCollector[i];
        if( item->Type() == aType )
            best = std::max( best, calcArea( item ) );
    }

    return best;
}


static inline double calcCommonArea( const BOARD_ITEM* aItem, const BOARD_ITEM* aOther )
{
    if( !aItem || !aOther )
        return 0;

    return getRect( aItem ).Common( getRect( aOther ) ).GetArea();
}


double calcRatio( double a, double b )
{
    if( a == 0.0 && b == 0.0 )
        return 1.0;

    if( b == 0.0 )
        return std::numeric_limits<double>::max();

    return a / b;
}


// The general idea here is that if the user clicks directly on a small item inside a larger
// one, then they want the small item.  The quintessential case of this is clicking on a pad
// within a footprint, but we also apply it for text within a footprint, footprints within
// larger footprints, and vias within either larger pads or longer tracks.
//
// These "guesses" presume there is area within the larger item to click in to select it.  If
// an item is mostly covered by smaller items within it, then the guesses are inappropriate as
// there might not be any area left to click to select the larger item.  In this case we must
// leave the items in the collector and bring up a Selection Clarification menu.
//
// We currently check for pads and text mostly covering a footprint, but we don't check for
// smaller footprints mostly covering a larger footprint.
//
void SELECTION_TOOL::GuessSelectionCandidates( GENERAL_COLLECTOR& aCollector,
                                               const VECTOR2I& aWhere ) const
{
    std::set<BOARD_ITEM*> preferred;
    std::set<BOARD_ITEM*> rejected;
    std::set<BOARD_ITEM*> forced;
    wxPoint               where( aWhere.x, aWhere.y );

    // footprints which are below this percentage of the largest footprint will be considered
    // for selection; all others will not
    constexpr double footprintToFootprintMinRatio = 0.20;
    // pads which are below this percentage of their parent's area will exclude their parent
    constexpr double padToFootprintMinRatio = 0.45;
    // footprints containing items with items-to-footprint area ratio higher than this will be
    // forced to stay on the list
    constexpr double footprintMaxCoverRatio = 0.90;
    constexpr double viaToPadMinRatio = 0.50;
    constexpr double trackViaLengthRatio = 2.0;
    constexpr double trackTrackLengthRatio = 0.3;
    constexpr double textToFeatureMinRatio = 0.2;
    constexpr double textToFootprintMinRatio = 0.4;
    // If the common area of two compared items is above the following threshold, they cannot
    // be rejected (it means they overlap and it might be hard to pick one by selecting
    // its unique area).
    constexpr double commonAreaRatio = 0.6;

    PCB_LAYER_ID activeLayer = (PCB_LAYER_ID) view()->GetTopLayer();
    LSET         silkLayers( 2, B_SilkS, F_SilkS );

    if( silkLayers[activeLayer] )
    {
        for( int i = 0; i < aCollector.GetCount(); ++i )
        {
            BOARD_ITEM* item = aCollector[i];
            KICAD_T type = item->Type();

            if( ( type == PCB_FP_TEXT_T || type == PCB_TEXT_T || type == PCB_SHAPE_T )
                    && silkLayers[item->GetLayer()] )
            {
                preferred.insert( item );
            }
        }

        if( preferred.size() > 0 )
        {
            aCollector.Empty();

            for( BOARD_ITEM* item : preferred )
                aCollector.Append( item );
            return;
        }
    }

    // Zone edges are very specific; zone fills much less so.
    if( aCollector.CountType( PCB_ZONE_AREA_T ) > 0 )
    {
        for( int i = aCollector.GetCount() - 1; i >= 0; i-- )
        {
            if( aCollector[i]->Type() == PCB_ZONE_AREA_T )
            {
                auto zone = static_cast<ZONE_CONTAINER*>( aCollector[i] );

                if( zone->HitTestForEdge( where, 5 * aCollector.GetGuide()->OnePixelInIU() ) )
                    preferred.insert( zone );
                else
                    rejected.insert( zone );
            }
        }

        if( preferred.size() > 0 )
        {
            aCollector.Empty();

            for( BOARD_ITEM* item : preferred )
                aCollector.Append( item );
            return;
        }
    }

    if( aCollector.CountType( PCB_FP_TEXT_T ) > 0 )
    {
        for( int i = 0; i < aCollector.GetCount(); ++i )
        {
            if( FP_TEXT* txt = dyn_cast<FP_TEXT*>( aCollector[i] ) )
            {
                double textArea = calcArea( txt );

                for( int j = 0; j < aCollector.GetCount(); ++j )
                {
                    if( i == j )
                        continue;

                    BOARD_ITEM* item = aCollector[j];
                    double itemArea = calcArea( item );
                    double areaRatio = calcRatio( textArea, itemArea );
                    double commonArea = calcCommonArea( txt, item );
                    double itemCommonRatio = calcRatio( commonArea, itemArea );
                    double txtCommonRatio = calcRatio( commonArea, textArea );

                    if( item->Type() == PCB_MODULE_T )
                    {
                        // when text area is small compared to an overlapping footprint,
                        // then it's a clear sign the text is the selection target
                        if( areaRatio < textToFootprintMinRatio && itemCommonRatio < commonAreaRatio )
                            rejected.insert( item );
                    }

                    switch( item->Type() )
                    {
                        case PCB_TRACE_T:
                        case PCB_ARC_T:
                        case PCB_PAD_T:
                        case PCB_SHAPE_T:
                        case PCB_VIA_T:
                        case PCB_MODULE_T:
                            if( areaRatio > textToFeatureMinRatio && txtCommonRatio < commonAreaRatio )
                                rejected.insert( txt );
                            break;
                        default:
                            break;
                    }
                }
            }
        }
    }

    if( aCollector.CountType( PCB_FP_SHAPE_T ) + aCollector.CountType( PCB_SHAPE_T ) > 1 )
    {
        // Prefer exact hits to sloppy ones
        int accuracy = KiROUND( 5 * aCollector.GetGuide()->OnePixelInIU() );
        bool found = false;

        for( int dist = 0; dist < accuracy; ++dist )
        {
            for( int i = 0; i < aCollector.GetCount(); ++i )
            {
                if( PCB_SHAPE* shape = dynamic_cast<PCB_SHAPE*>( aCollector[i] ) )
                {
                    if( shape->HitTest( where, dist ) )
                    {
                        found = true;
                        break;
                    }
                }
            }

            if( found )
            {
                // throw out everything that is more sloppy than what we found
                for( int i = 0; i < aCollector.GetCount(); ++i )
                {
                    if( PCB_SHAPE* shape = dynamic_cast<PCB_SHAPE*>( aCollector[i] ) )
                    {
                        if( !shape->HitTest( where, dist ) )
                            rejected.insert( shape );
                    }
                }

                // we're done now
                break;
            }
        }
    }

    if( aCollector.CountType( PCB_PAD_T ) > 0 )
    {
        for( int i = 0; i < aCollector.GetCount(); ++i )
        {
            if( D_PAD* pad = dyn_cast<D_PAD*>( aCollector[i] ) )
            {
                MODULE* parent = pad->GetParent();
                double ratio = calcRatio( calcArea( pad ), calcArea( parent ) );

                // when pad area is small compared to the parent footprint,
                // then it is a clear sign the pad is the selection target
                if( ratio < padToFootprintMinRatio )
                    rejected.insert( pad->GetParent() );
            }
        }
    }

    bool hasNonModules = false;

    for( int i = 0; i < aCollector.GetCount(); ++i )
    {
        if( aCollector[i]->Type() != PCB_MODULE_T )
        {
            hasNonModules = true;
            break;
        }
    }

    if( aCollector.CountType( PCB_MODULE_T ) > 0 )
    {
        double maxArea = calcMaxArea( aCollector, PCB_MODULE_T );
        BOX2D viewportD = getView()->GetViewport();
        BOX2I viewport( VECTOR2I( viewportD.GetPosition() ), VECTOR2I( viewportD.GetSize() ) );
        double maxCoverRatio = footprintMaxCoverRatio;

        // MODULE::CoverageRatio() doesn't take zone handles & borders into account so just
        // use a more aggressive cutoff point if zones are involved.
        if(  aCollector.CountType( PCB_ZONE_AREA_T ) )
            maxCoverRatio /= 2;

        for( int i = 0; i < aCollector.GetCount(); ++i )
        {
            if( MODULE* mod = dyn_cast<MODULE*>( aCollector[i] ) )
            {
                // filter out components larger than the viewport
                if( mod->ViewBBox().GetHeight() > viewport.GetHeight() ||
                    mod->ViewBBox().GetWidth() > viewport.GetWidth() )
                    rejected.insert( mod );
                // footprints completely covered with other features have no other
                // means of selection, so must be kept
                else if( mod->CoverageRatio( aCollector ) > maxCoverRatio )
                    rejected.erase( mod );
                // if a footprint is much smaller than the largest overlapping
                // footprint then it should be considered for selection
                else if( calcRatio( calcArea( mod ), maxArea ) <= footprintToFootprintMinRatio )
                    continue;
                // reject ALL OTHER footprints if there's still something else left
                // to select
                else if( hasNonModules )
                    rejected.insert( mod );
            }
        }
    }

    if( aCollector.CountType( PCB_VIA_T ) > 0 )
    {
        for( int i = 0; i < aCollector.GetCount(); ++i )
        {
            if( VIA* via = dyn_cast<VIA*>( aCollector[i] ) )
            {
                double viaArea = calcArea( via );

                for( int j = 0; j < aCollector.GetCount(); ++j )
                {
                    if( i == j )
                        continue;

                    BOARD_ITEM* item = aCollector[j];
                    double areaRatio = calcRatio( viaArea, calcArea( item ) );

                    if( item->Type() == PCB_MODULE_T && areaRatio < padToFootprintMinRatio )
                        rejected.insert( item );

                    if( item->Type() == PCB_PAD_T && areaRatio < viaToPadMinRatio )
                        rejected.insert( item );

                    if( TRACK* track = dyn_cast<TRACK*>( item ) )
                    {
                        if( track->GetNetCode() != via->GetNetCode() )
                            continue;

                        double lenRatio = (double) ( track->GetLength() + track->GetWidth() ) /
                                          (double) via->GetWidth();

                        if( lenRatio > trackViaLengthRatio )
                            rejected.insert( track );
                    }
                }
            }
        }
    }

    int nTracks = aCollector.CountType( PCB_TRACE_T );

    if( nTracks > 0 )
    {
        double maxLength = 0.0;
        double minLength = std::numeric_limits<double>::max();
        double maxArea = 0.0;
        const TRACK* maxTrack = nullptr;

        for( int i = 0; i < aCollector.GetCount(); ++i )
        {
            if( TRACK* track = dyn_cast<TRACK*>( aCollector[i] ) )
            {
                maxLength = std::max( track->GetLength(), maxLength );
                maxLength = std::max( (double) track->GetWidth(), maxLength );

                minLength = std::min( std::max( track->GetLength(), (double) track->GetWidth() ), minLength );

                double area = track->GetLength() * track->GetWidth();

                if( area > maxArea )
                {
                    maxArea = area;
                    maxTrack = track;
                }
            }
        }

        if( maxLength > 0.0 && minLength / maxLength < trackTrackLengthRatio && nTracks > 1 )
        {
            for( int i = 0; i < aCollector.GetCount(); ++i )
             {
                if( TRACK* track = dyn_cast<TRACK*>( aCollector[i] ) )
                {
                    double ratio = std::max( (double) track->GetWidth(), track->GetLength() ) / maxLength;

                    if( ratio > trackTrackLengthRatio )
                        rejected.insert( track );
                }
            }
        }

        for( int j = 0; j < aCollector.GetCount(); ++j )
        {
            if( MODULE* mod = dyn_cast<MODULE*>( aCollector[j] ) )
            {
                double ratio = calcRatio( maxArea, mod->GetFootprintRect().GetArea() );

                if( ratio < padToFootprintMinRatio && calcCommonArea( maxTrack, mod ) < commonAreaRatio )
                    rejected.insert( mod );
            }
        }
    }

    if( (unsigned) aCollector.GetCount() > rejected.size() )  // do not remove everything
    {
        for( BOARD_ITEM* item : rejected )
        {
            aCollector.Transfer( item );
        }
    }
}


void SELECTION_TOOL::FilterCollectorForGroups( GENERAL_COLLECTOR& aCollector ) const
{
    std::unordered_set<BOARD_ITEM*> toAdd;

    // If any element is a member of a group, replace those elements with the top containing group.
    for( int j = 0; j < aCollector.GetCount(); )
    {
        BOARD_ITEM* item = aCollector[j];
        PCB_GROUP*  aTop = PCB_GROUP::TopLevelGroup( item, m_enteredGroup );

        if( aTop != NULL )
        {
            if( aTop != item )
            {
                toAdd.insert( aTop );
                aCollector.Remove( item );
                continue;
            }
        }
        else if( m_enteredGroup && !PCB_GROUP::WithinScope( item, m_enteredGroup ) )
        {
            // If a group is entered, disallow selections of objects outside the group.
            aCollector.Remove( item );
            continue;
        }

        ++j;
    }

    for( BOARD_ITEM* item : toAdd )
    {
        if( !aCollector.HasItem( item ) )
        {
            aCollector.Append( item );
        }
    }
}


int SELECTION_TOOL::updateSelection( const TOOL_EVENT& aEvent )
{
    getView()->Update( &m_selection );
    getView()->Update( &m_enteredGroupOverlay );

    return 0;
}


int SELECTION_TOOL::UpdateMenu( const TOOL_EVENT& aEvent )
{
    ACTION_MENU*      actionMenu = aEvent.Parameter<ACTION_MENU*>();
    CONDITIONAL_MENU* conditionalMenu = dynamic_cast<CONDITIONAL_MENU*>( actionMenu );

    if( conditionalMenu )
        conditionalMenu->Evaluate( m_selection );

    if( actionMenu )
        actionMenu->UpdateAll();

    return 0;
}


void SELECTION_TOOL::setTransitions()
{
    Go( &SELECTION_TOOL::UpdateMenu,          ACTIONS::updateMenu.MakeEvent() );

    Go( &SELECTION_TOOL::Main,                PCB_ACTIONS::selectionActivate.MakeEvent() );
    Go( &SELECTION_TOOL::CursorSelection,     PCB_ACTIONS::selectionCursor.MakeEvent() );
    Go( &SELECTION_TOOL::ClearSelection,      PCB_ACTIONS::selectionClear.MakeEvent() );

    Go( &SELECTION_TOOL::SelectItem,          PCB_ACTIONS::selectItem.MakeEvent() );
    Go( &SELECTION_TOOL::SelectItems,         PCB_ACTIONS::selectItems.MakeEvent() );
    Go( &SELECTION_TOOL::UnselectItem,        PCB_ACTIONS::unselectItem.MakeEvent() );
    Go( &SELECTION_TOOL::UnselectItems,       PCB_ACTIONS::unselectItems.MakeEvent() );
    Go( &SELECTION_TOOL::SelectionMenu,       PCB_ACTIONS::selectionMenu.MakeEvent() );

    Go( &SELECTION_TOOL::find,                ACTIONS::find.MakeEvent() );

    Go( &SELECTION_TOOL::filterSelection,     PCB_ACTIONS::filterSelection.MakeEvent() );
    Go( &SELECTION_TOOL::expandConnection,    PCB_ACTIONS::selectConnection.MakeEvent() );
    Go( &SELECTION_TOOL::selectNet,           PCB_ACTIONS::selectNet.MakeEvent() );
    Go( &SELECTION_TOOL::selectNet,           PCB_ACTIONS::deselectNet.MakeEvent() );
    Go( &SELECTION_TOOL::selectSameSheet,     PCB_ACTIONS::selectSameSheet.MakeEvent() );
    Go( &SELECTION_TOOL::selectSheetContents, PCB_ACTIONS::selectOnSheetFromEeschema.MakeEvent() );
    Go( &SELECTION_TOOL::updateSelection,     EVENTS::SelectedItemsModified );
    Go( &SELECTION_TOOL::updateSelection,     EVENTS::SelectedItemsMoved );

    Go( &SELECTION_TOOL::SelectAll,           ACTIONS::selectAll.MakeEvent() );
}
