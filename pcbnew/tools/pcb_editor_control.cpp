/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2014 CERN
 * Copyright (C) 2014-2020 KiCad Developers, see AUTHORS.txt for contributors.
 * @author Maciej Suminski <maciej.suminski@cern.ch>
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
#include "pcb_editor_control.h"
#include "drawing_tool.h"
#include "pcb_actions.h"
#include "pcbnew_picker_tool.h"
#include "selection_tool.h"
#include "edit_tool.h"
#include "tool/tool_event.h"
#include <bitmaps.h>
#include <board_commit.h>
#include <class_board.h>
#include <class_pcb_group.h>
#include <class_module.h>
#include <class_pcb_target.h>
#include <class_track.h>
#include <class_zone.h>
#include <class_marker_pcb.h>
#include <collectors.h>
#include <confirm.h>
#include <cstdint>
#include <dialogs/dialog_page_settings.h>
#include <dialogs/dialog_update_pcb.h>
#include <functional>
#include <gestfich.h>
#include <kiface_i.h>
#include <kiway.h>
#include <memory>
#include <netlist_reader/pcb_netlist.h>
#include <origin_viewitem.h>
#include <painter.h>
#include <pcb_edit_frame.h>
#include <pcbnew_id.h>
#include <pcbnew_settings.h>
#include <project.h>
#include <project/project_file.h> // LAST_PATH_TYPE
#include <tool/tool_manager.h>
#include <tools/tool_event_utils.h>
#include <view/view_controls.h>
#include <view/view_group.h>
#include <wildcards_and_files_ext.h>
#include <ws_proxy_undo_item.h>
#include <footprint_edit_frame.h>

using namespace std::placeholders;


class ZONE_CONTEXT_MENU : public ACTION_MENU
{
public:
    ZONE_CONTEXT_MENU() :
        ACTION_MENU( true )
    {
        SetIcon( add_zone_xpm );
        SetTitle( _( "Zones" ) );

        Add( PCB_ACTIONS::zoneFill );
        Add( PCB_ACTIONS::zoneFillAll );
        Add( PCB_ACTIONS::zoneUnfill );
        Add( PCB_ACTIONS::zoneUnfillAll );

        AppendSeparator();

        Add( PCB_ACTIONS::zoneMerge );
        Add( PCB_ACTIONS::zoneDuplicate );
        Add( PCB_ACTIONS::drawZoneCutout );
        Add( PCB_ACTIONS::drawSimilarZone );
    }


protected:
    ACTION_MENU* create() const override
    {
        return new ZONE_CONTEXT_MENU();
    }
};


class LOCK_CONTEXT_MENU : public ACTION_MENU
{
public:
    LOCK_CONTEXT_MENU() :
        ACTION_MENU( true )
    {
        SetIcon( locked_xpm );
        SetTitle( _( "Locking" ) );

        Add( PCB_ACTIONS::lock );
        Add( PCB_ACTIONS::unlock );
        Add( PCB_ACTIONS::toggleLock );
    }

    ACTION_MENU* create() const override
    {
        return new LOCK_CONTEXT_MENU();
    }
};


class GROUP_CONTEXT_MENU : public ACTION_MENU
{
public:
    GROUP_CONTEXT_MENU( ) : ACTION_MENU( true )
    {
        SetIcon( locked_xpm ); // fixme
        SetTitle( _( "Grouping" ) );

        Add( PCB_ACTIONS::groupCreate );
        Add( PCB_ACTIONS::groupUngroup );
        Add( PCB_ACTIONS::groupMerge );
        Add( PCB_ACTIONS::groupRemoveItems );
        Add( PCB_ACTIONS::groupFlatten );
        Add( PCB_ACTIONS::groupEnter );
    }

    ACTION_MENU* create() const override
    {
        return new GROUP_CONTEXT_MENU();
    }

private:
    void update() override
    {
        SELECTION_TOOL* selTool = getToolManager()->GetTool<SELECTION_TOOL>();
        BOARD* board = selTool->GetBoard();

        const auto& selection = selTool->GetSelection();

        wxString check = board->GroupsSanityCheck();
        wxCHECK_RET( check == wxEmptyString, _( "Group is in inconsistent state: " ) + check );

        BOARD::GroupLegalOpsField legalOps = board->GroupLegalOps( selection );

        Enable( PCB_ACTIONS::groupCreate.GetUIId(),      legalOps.create );
        Enable( PCB_ACTIONS::groupMerge.GetUIId(),       legalOps.merge );
        Enable( PCB_ACTIONS::groupUngroup.GetUIId(),     legalOps.ungroup );
        Enable( PCB_ACTIONS::groupRemoveItems.GetUIId(), legalOps.removeItems );
        Enable( PCB_ACTIONS::groupFlatten.GetUIId(),     legalOps.flatten );
        Enable( PCB_ACTIONS::groupEnter.GetUIId(),       legalOps.enter );
    }
};


PCB_EDITOR_CONTROL::PCB_EDITOR_CONTROL() :
    PCB_TOOL_BASE( "pcbnew.EditorControl" ),
    m_frame( nullptr )
{
    m_placeOrigin = std::make_unique<KIGFX::ORIGIN_VIEWITEM>( KIGFX::COLOR4D( 0.8, 0.0, 0.0, 1.0 ),
                                                KIGFX::ORIGIN_VIEWITEM::CIRCLE_CROSS );
}


PCB_EDITOR_CONTROL::~PCB_EDITOR_CONTROL()
{
}


void PCB_EDITOR_CONTROL::Reset( RESET_REASON aReason )
{
    m_frame = getEditFrame<PCB_EDIT_FRAME>();

    if( aReason == MODEL_RELOAD || aReason == GAL_SWITCH )
    {
        m_placeOrigin->SetPosition( getModel<BOARD>()->GetDesignSettings().m_AuxOrigin );
        getView()->Remove( m_placeOrigin.get() );
        getView()->Add( m_placeOrigin.get() );
    }
}


bool PCB_EDITOR_CONTROL::Init()
{
    auto activeToolCondition = [ this ] ( const SELECTION& aSel ) {
        return ( !m_frame->ToolStackIsEmpty() );
    };

    auto inactiveStateCondition = [ this ] ( const SELECTION& aSel ) {
        return ( m_frame->ToolStackIsEmpty() && aSel.Size() == 0 );
    };

    auto placeModuleCondition = [ this ] ( const SELECTION& aSel ) {
        return ( m_frame->IsCurrentTool( PCB_ACTIONS::placeModule ) && aSel.GetSize() == 0 );
    };

    auto& ctxMenu = m_menu.GetMenu();

    // "Cancel" goes at the top of the context menu when a tool is active
    ctxMenu.AddItem( ACTIONS::cancelInteractive, activeToolCondition, 1 );
    ctxMenu.AddSeparator( 1 );

    // "Get and Place Footprint" should be available for Place Footprint tool
    ctxMenu.AddItem( PCB_ACTIONS::getAndPlace, placeModuleCondition, 1000 );
    ctxMenu.AddSeparator( 1000 );

    // Finally, add the standard zoom & grid items
    getEditFrame<PCB_BASE_FRAME>()->AddStandardSubMenus( m_menu );

    auto zoneMenu = std::make_shared<ZONE_CONTEXT_MENU>();
    zoneMenu->SetTool( this );

    auto lockMenu = std::make_shared<LOCK_CONTEXT_MENU>();
    lockMenu->SetTool( this );

    auto groupMenu = std::make_shared<GROUP_CONTEXT_MENU>();
    groupMenu->SetTool( this );

    // Add the PCB control menus to relevant other tools

    SELECTION_TOOL* selTool = m_toolMgr->GetTool<SELECTION_TOOL>();

    if( selTool )
    {
        auto& toolMenu = selTool->GetToolMenu();
        auto& menu = toolMenu.GetMenu();

        // Add "Get and Place Footprint" when Selection tool is in an inactive state
        menu.AddItem( PCB_ACTIONS::getAndPlace, inactiveStateCondition );
        menu.AddSeparator();

        toolMenu.AddSubMenu( zoneMenu );
        toolMenu.AddSubMenu( lockMenu );
        toolMenu.AddSubMenu( groupMenu );

        menu.AddMenu( groupMenu.get(), SELECTION_CONDITIONS::NotEmpty, 100 );
        menu.AddMenu( lockMenu.get(), SELECTION_CONDITIONS::OnlyTypes( GENERAL_COLLECTOR::LockableItems ), 100 );

        menu.AddMenu( zoneMenu.get(), SELECTION_CONDITIONS::OnlyType( PCB_ZONE_AREA_T ), 200 );
    }

    DRAWING_TOOL* drawingTool = m_toolMgr->GetTool<DRAWING_TOOL>();

    if( drawingTool )
    {
        auto& toolMenu = drawingTool->GetToolMenu();
        auto& menu = toolMenu.GetMenu();

        toolMenu.AddSubMenu( zoneMenu );

        // Functor to say if the PCB_EDIT_FRAME is in a given mode
        // Capture the tool pointer and tool mode by value
        auto toolActiveFunctor = [=]( DRAWING_TOOL::MODE aMode )
        {
            return [=]( const SELECTION& sel )
            {
                return drawingTool->GetDrawingMode() == aMode;
            };
        };

        menu.AddMenu( zoneMenu.get(), toolActiveFunctor( DRAWING_TOOL::MODE::ZONE ), 200 );
    }

    return true;
}


int PCB_EDITOR_CONTROL::New( const TOOL_EVENT& aEvent )
{
    m_frame->Files_io_from_id( ID_NEW_BOARD );
    return 0;
}


int PCB_EDITOR_CONTROL::Open( const TOOL_EVENT& aEvent )
{
    m_frame->Files_io_from_id( ID_LOAD_FILE );
    return 0;
}


int PCB_EDITOR_CONTROL::Save( const TOOL_EVENT& aEvent )
{
    m_frame->Files_io_from_id( ID_SAVE_BOARD );
    return 0;
}


int PCB_EDITOR_CONTROL::SaveAs( const TOOL_EVENT& aEvent )
{
    m_frame->Files_io_from_id( ID_SAVE_BOARD_AS );
    return 0;
}


int PCB_EDITOR_CONTROL::SaveCopyAs( const TOOL_EVENT& aEvent )
{
    m_frame->Files_io_from_id( ID_COPY_BOARD_AS );
    return 0;
}


int PCB_EDITOR_CONTROL::PageSettings( const TOOL_EVENT& aEvent )
{
    PICKED_ITEMS_LIST   undoCmd;
    WS_PROXY_UNDO_ITEM* undoItem = new WS_PROXY_UNDO_ITEM( m_frame );
    ITEM_PICKER         wrapper( nullptr, undoItem, UNDO_REDO::PAGESETTINGS );

    undoCmd.PushItem( wrapper );
    m_frame->SaveCopyInUndoList( undoCmd, UNDO_REDO::PAGESETTINGS );

    DIALOG_PAGES_SETTINGS dlg( m_frame, wxSize( MAX_PAGE_SIZE_PCBNEW_MILS,
                                                MAX_PAGE_SIZE_PCBNEW_MILS ) );
    dlg.SetWksFileName( BASE_SCREEN::m_PageLayoutDescrFileName );

    if( dlg.ShowModal() != wxID_OK )
        m_frame->RollbackFromUndo();

    return 0;
}


int PCB_EDITOR_CONTROL::Plot( const TOOL_EVENT& aEvent )
{
    m_frame->ToPlotter( ID_GEN_PLOT );
    return 0;
}


int PCB_EDITOR_CONTROL::BoardSetup( const TOOL_EVENT& aEvent )
{
    getEditFrame<PCB_EDIT_FRAME>()->ShowBoardSetupDialog();
    return 0;
}


int PCB_EDITOR_CONTROL::ImportNetlist( const TOOL_EVENT& aEvent )
{
    getEditFrame<PCB_EDIT_FRAME>()->InstallNetlistFrame();
    return 0;
}


int PCB_EDITOR_CONTROL::ImportSpecctraSession( const TOOL_EVENT& aEvent )
{
    wxString fullFileName = frame()->GetBoard()->GetFileName();
    wxString path;
    wxString name;
    wxString ext;

    wxFileName::SplitPath( fullFileName, &path, &name, &ext );
    name += wxT( ".ses" );

    fullFileName = EDA_FILE_SELECTOR( _( "Merge Specctra Session file:" ), path, name,
                                      wxT( ".ses" ), wxT( "*.ses" ), frame(), wxFD_OPEN, false );

    if( !fullFileName.IsEmpty() )
        getEditFrame<PCB_EDIT_FRAME>()->ImportSpecctraSession( fullFileName );

    return 0;
}


int PCB_EDITOR_CONTROL::ExportSpecctraDSN( const TOOL_EVENT& aEvent )
{
    wxString    fullFileName = m_frame->GetLastPath( LAST_PATH_SPECCTRADSN );
    wxFileName  fn;

    if( fullFileName.IsEmpty() )
    {
        fn = m_frame->GetBoard()->GetFileName();
        fn.SetExt( SpecctraDsnFileExtension );
    }
    else
        fn = fullFileName;

    fullFileName = EDA_FILE_SELECTOR( _( "Specctra DSN File" ), fn.GetPath(), fn.GetFullName(),
                                      SpecctraDsnFileExtension, SpecctraDsnFileWildcard(),
                                      frame(), wxFD_SAVE | wxFD_OVERWRITE_PROMPT, false );

    if( !fullFileName.IsEmpty() )
    {
        m_frame->SetLastPath( LAST_PATH_SPECCTRADSN, fullFileName );
        getEditFrame<PCB_EDIT_FRAME>()->ExportSpecctraFile( fullFileName );
    }

    return 0;
}


int PCB_EDITOR_CONTROL::GenerateFabFiles( const TOOL_EVENT& aEvent )
{
    wxCommandEvent dummy;

    if( aEvent.IsAction( &PCB_ACTIONS::generateGerbers ) )
        m_frame->ToPlotter( ID_GEN_PLOT_GERBER );
    else if( aEvent.IsAction( &PCB_ACTIONS::generateReportFile ) )
        m_frame->GenFootprintsReport( dummy );
    else if( aEvent.IsAction( &PCB_ACTIONS::generateD356File ) )
        m_frame->GenD356File( dummy );
    else if( aEvent.IsAction( &PCB_ACTIONS::generateBOM ) )
        m_frame->RecreateBOMFileFromBoard( dummy );
    else
        wxFAIL_MSG( "GenerateFabFiles(): unexpected request" );

    return 0;
}


int PCB_EDITOR_CONTROL::RepairBoard( const TOOL_EVENT& aEvent )
{
    int      errors = 0;
    wxString details;

    /*******************************
     * Repair duplicate IDs
     */

    std::set<KIID> ids;
    int            duplicates = 0;

    auto processItem = [&]( EDA_ITEM* aItem )
                       {
                           if( ids.count( aItem->m_Uuid ) )
                           {
                               duplicates++;
                               const_cast<KIID&>( aItem->m_Uuid ) = KIID();
                           }

                           ids.insert( aItem->m_Uuid );
                       };

    // Module IDs are the most important, so give them the first crack at "owning" a particular
    // KIID.

    for( MODULE* module : board()->Modules() )
        processItem( module );

    // After that the principal use is for DRC marker pointers, which are most likely to pads
    // or tracks.

    for( MODULE* module : board()->Modules() )
    {
        for( D_PAD* pad : module->Pads() )
            processItem( pad );
    }

    for( TRACK* track : board()->Tracks() )
        processItem( track );

    // From here out I don't think order matters much.

    for( MODULE* module : board()->Modules() )
    {
        processItem( &module->Reference() );
        processItem( &module->Value() );

        for( BOARD_ITEM* item : module->GraphicalItems() )
            processItem( item );

        for( ZONE_CONTAINER* zone : module->Zones() )
            processItem( zone );
    }

    for( BOARD_ITEM* drawing : board()->Drawings() )
        processItem( drawing );

    for( ZONE_CONTAINER* zone : board()->Zones() )
        processItem( zone );

    for( MARKER_PCB* marker : board()->Markers() )
        processItem( marker );

    if( duplicates )
    {
        errors += duplicates;
        details += wxString::Format( _( "%d duplicate IDs replaced.\n" ), duplicates );
    }

    /*******************************
     * Your test here
     */

    /*******************************
     * Inform the user
     */

    if( errors )
    {
        m_frame->OnModify();

        wxString msg = wxString::Format( _( "%d potential problems repaired." ), errors );
        DisplayInfoMessage( m_frame, msg, details );
    }
    else
    {
        DisplayInfoMessage( m_frame, _( "No board problems found." ) );
    }

    return 0;
}


int PCB_EDITOR_CONTROL::UpdatePCBFromSchematic( const TOOL_EVENT& aEvent )
{
    NETLIST netlist;

    if( m_frame->FetchNetlistFromSchematic( netlist, PCB_EDIT_FRAME::ANNOTATION_DIALOG ) )
    {
        DIALOG_UPDATE_PCB updateDialog( m_frame, &netlist );
        updateDialog.ShowModal();
    }

    return 0;
}

int PCB_EDITOR_CONTROL::UpdateSchematicFromPCB( const TOOL_EVENT& aEvent )
{
    if( Kiface().IsSingle() )
    {
        DisplayErrorMessage(
                m_frame, _( "Cannot update schematic because Pcbnew is opened in stand-alone "
                            "mode. In order to create or update PCBs from schematics, you "
                            "must launch the KiCad project manager and create a project." ) );
        return 0;
    }

    m_frame->RunEeschema();
    KIWAY_PLAYER* frame = m_frame->Kiway().Player( FRAME_SCH, false );

    if( frame )
    {
        std::string payload;
        m_frame->Kiway().ExpressMail( FRAME_SCH, MAIL_SCH_UPDATE, payload, m_frame );
    }
    return 0;
}


int PCB_EDITOR_CONTROL::ShowEeschema( const TOOL_EVENT& aEvent )
{
    m_frame->RunEeschema();
    return 0;
}


int PCB_EDITOR_CONTROL::ToggleLayersManager( const TOOL_EVENT& aEvent )
{
    getEditFrame<PCB_EDIT_FRAME>()->ToggleLayersManager();
    return 0;
}


int PCB_EDITOR_CONTROL::ToggleMicrowaveToolbar( const TOOL_EVENT& aEvent )
{
    getEditFrame<PCB_EDIT_FRAME>()->ToggleMicrowaveToolbar();
    return 0;
}


int PCB_EDITOR_CONTROL::TogglePythonConsole( const TOOL_EVENT& aEvent )
{
#if defined( KICAD_SCRIPTING_WXPYTHON )
    m_frame->ScriptingConsoleEnableDisable();
#endif
    return 0;
}


// Track & via size control
int PCB_EDITOR_CONTROL::TrackWidthInc( const TOOL_EVENT& aEvent )
{
    BOARD_DESIGN_SETTINGS& designSettings = getModel<BOARD>()->GetDesignSettings();
    constexpr KICAD_T      types[] = { PCB_TRACE_T, PCB_VIA_T, EOT };
    PCBNEW_SELECTION&      selection = m_toolMgr->GetTool<SELECTION_TOOL>()->GetSelection();

    if( m_frame->ToolStackIsEmpty() && SELECTION_CONDITIONS::OnlyTypes( types )( selection ) )
    {
        BOARD_COMMIT commit( this );

        for( EDA_ITEM* item : selection )
        {
            if( item->Type() == PCB_TRACE_T )
            {
                TRACK* track = (TRACK*) item;

                for( int candidate : designSettings.m_TrackWidthList )
                {
                    if( candidate > track->GetWidth() )
                    {
                        commit.Modify( track );
                        track->SetWidth( candidate );
                        break;
                    }
                }
            }
        }

        commit.Push( "Increase Track Width" );
    }
    else
    {
        int widthIndex = designSettings.GetTrackWidthIndex() + 1;

        // If we go past the last track width entry in the list, start over at the beginning
        if( widthIndex >= (int) designSettings.m_TrackWidthList.size() )
        {
            widthIndex = 0;
        }

        designSettings.SetTrackWidthIndex( widthIndex );
        designSettings.UseCustomTrackViaSize( false );

        m_toolMgr->RunAction( PCB_ACTIONS::trackViaSizeChanged, true );
    }

    return 0;
}


int PCB_EDITOR_CONTROL::TrackWidthDec( const TOOL_EVENT& aEvent )
{
    BOARD_DESIGN_SETTINGS& designSettings = getModel<BOARD>()->GetDesignSettings();
    constexpr KICAD_T      types[] = { PCB_TRACE_T, PCB_VIA_T, EOT };
    PCBNEW_SELECTION&      selection = m_toolMgr->GetTool<SELECTION_TOOL>()->GetSelection();

    if( m_frame->ToolStackIsEmpty() && SELECTION_CONDITIONS::OnlyTypes( types )( selection ) )
    {
        BOARD_COMMIT commit( this );

        for( EDA_ITEM* item : selection )
        {
            if( item->Type() == PCB_TRACE_T )
            {
                TRACK* track = (TRACK*) item;

                for( int i = designSettings.m_TrackWidthList.size() - 1; i >= 0; --i )
                {
                    int candidate = designSettings.m_TrackWidthList[ i ];

                    if( candidate < track->GetWidth() )
                    {
                        commit.Modify( track );
                        track->SetWidth( candidate );
                        break;
                    }
                }
            }
        }

        commit.Push( "Decrease Track Width" );
    }
    else
    {
        int widthIndex = 0; // Assume we only have a single track width entry

        // If there are more, cycle through them backwards
        if( designSettings.m_TrackWidthList.size() > 0 )
        {
            widthIndex = designSettings.GetTrackWidthIndex() - 1;
            // If we get to the lowest entry start over at the highest
            if( widthIndex < 0 )
                widthIndex = designSettings.m_TrackWidthList.size() - 1;
        }

        designSettings.SetTrackWidthIndex( widthIndex );
        designSettings.UseCustomTrackViaSize( false );

        m_toolMgr->RunAction( PCB_ACTIONS::trackViaSizeChanged, true );
    }

    return 0;
}


int PCB_EDITOR_CONTROL::ViaSizeInc( const TOOL_EVENT& aEvent )
{
    BOARD_DESIGN_SETTINGS& designSettings = getModel<BOARD>()->GetDesignSettings();
    constexpr KICAD_T      types[] = { PCB_TRACE_T, PCB_VIA_T, EOT };
    PCBNEW_SELECTION&      selection = m_toolMgr->GetTool<SELECTION_TOOL>()->GetSelection();

    if( m_frame->ToolStackIsEmpty() && SELECTION_CONDITIONS::OnlyTypes( types )( selection ) )
    {
        BOARD_COMMIT commit( this );

        for( EDA_ITEM* item : selection )
        {
            if( item->Type() == PCB_VIA_T )
            {
                VIA* via = (VIA*) item;

                for( VIA_DIMENSION candidate : designSettings.m_ViasDimensionsList )
                {
                    if( candidate.m_Diameter > via->GetWidth() )
                    {
                        commit.Modify( via );
                        via->SetWidth( candidate.m_Diameter );
                        via->SetDrill( candidate.m_Drill );
                        break;
                    }
                }
            }
        }

        commit.Push( "Increase Via Size" );
    }
    else
    {
        int sizeIndex = designSettings.GetViaSizeIndex() + 1;

        // If we go past the last via entry in the list, start over at the beginning
        if( sizeIndex >= (int) designSettings.m_ViasDimensionsList.size() )
            sizeIndex = 0;

        designSettings.SetViaSizeIndex( sizeIndex );
        designSettings.UseCustomTrackViaSize( false );

        m_toolMgr->RunAction( PCB_ACTIONS::trackViaSizeChanged, true );
    }

    return 0;
}


int PCB_EDITOR_CONTROL::ViaSizeDec( const TOOL_EVENT& aEvent )
{
    BOARD_DESIGN_SETTINGS& designSettings = getModel<BOARD>()->GetDesignSettings();
    constexpr KICAD_T      types[] = { PCB_TRACE_T, PCB_VIA_T, EOT };
    PCBNEW_SELECTION&      selection = m_toolMgr->GetTool<SELECTION_TOOL>()->GetSelection();

    if( m_frame->ToolStackIsEmpty() && SELECTION_CONDITIONS::OnlyTypes( types )( selection ) )
    {
        BOARD_COMMIT commit( this );

        for( EDA_ITEM* item : selection )
        {
            if( item->Type() == PCB_VIA_T )
            {
                VIA* via = (VIA*) item;

                for( int i = designSettings.m_ViasDimensionsList.size() - 1; i >= 0; --i )
                {
                    VIA_DIMENSION candidate = designSettings.m_ViasDimensionsList[ i ];

                    if( candidate.m_Diameter < via->GetWidth() )
                    {
                        commit.Modify( via );
                        via->SetWidth( candidate.m_Diameter );
                        via->SetDrill( candidate.m_Drill );
                        break;
                    }
                }
            }
        }

        commit.Push( "Decrease Via Size" );
    }
    else
    {
        int sizeIndex = 0; // Assume we only have a single via size entry

        // If there are more, cycle through them backwards
        if( designSettings.m_ViasDimensionsList.size() > 0 )
        {
            sizeIndex = designSettings.GetViaSizeIndex() - 1;

            // If we get to the lowest entry start over at the highest
            if( sizeIndex < 0 )
                sizeIndex = designSettings.m_ViasDimensionsList.size() - 1;
        }

        designSettings.SetViaSizeIndex( sizeIndex );
        designSettings.UseCustomTrackViaSize( false );

        m_toolMgr->RunAction( PCB_ACTIONS::trackViaSizeChanged, true );
    }

    return 0;
}


int PCB_EDITOR_CONTROL::PlaceModule( const TOOL_EVENT& aEvent )
{
    MODULE* module = aEvent.Parameter<MODULE*>();
    KIGFX::VIEW_CONTROLS* controls = getViewControls();
    BOARD_COMMIT commit( m_frame );
    BOARD* board = getModel<BOARD>();

    m_toolMgr->RunAction( PCB_ACTIONS::selectionClear, true );
    controls->ShowCursor( true );

    std::string tool = aEvent.GetCommandStr().get();
    m_frame->PushTool( tool );
    Activate();

    VECTOR2I cursorPos = controls->GetCursorPosition();
    bool     reselect = false;
    bool     fromOtherCommand = module != nullptr;

    // Prime the pump
    if( module )
    {
        module->SetPosition( wxPoint( cursorPos.x, cursorPos.y ) );
        m_toolMgr->RunAction( PCB_ACTIONS::selectItem, true, module );
        m_toolMgr->RunAction( ACTIONS::refreshPreview );
    }
    else if( aEvent.HasPosition() )
        m_toolMgr->RunAction( PCB_ACTIONS::cursorClick );

    // Main loop: keep receiving events
    while( TOOL_EVENT* evt = Wait() )
    {
        m_frame->GetCanvas()->SetCurrentCursor( wxCURSOR_PENCIL );
        cursorPos = controls->GetCursorPosition( !evt->Modifier( MD_ALT ) );

        if( reselect && module )
            m_toolMgr->RunAction( PCB_ACTIONS::selectItem, true, module );

        auto cleanup = [&] ()
        {
            m_toolMgr->RunAction( PCB_ACTIONS::selectionClear, true );
            commit.Revert();

            if( fromOtherCommand )
            {
                PICKED_ITEMS_LIST* undo = m_frame->PopCommandFromUndoList();

                if( undo )
                {
                    m_frame->PutDataInPreviousState( undo, false );
                    undo->ClearListAndDeleteItems();
                    delete undo;
                }
            }

            module = NULL;
        };

        if( evt->IsCancelInteractive() )
        {
            if( module )
                cleanup();
            else
            {
                m_frame->PopTool( tool );
                break;
            }
        }

        else if( evt->IsActivate() )
        {
            if( module )
                cleanup();

            if( evt->IsMoveTool() )
            {
                // leave ourselves on the stack so we come back after the move
                break;
            }
            else
            {
                frame()->PopTool( tool );
                break;
            }
        }

        else if( evt->IsClick( BUT_LEFT ) )
        {
            if( !module )
            {
                // Pick the module to be placed
                module = m_frame->SelectFootprintFromLibTree();

                if( module == NULL )
                    continue;

                module->SetLink( niluuid );

                module->SetFlags( IS_NEW ); // whatever

                // Set parent so that clearance can be loaded
                module->SetParent( board );

                // Put it on FRONT layer,
                // (Can be stored flipped if the lib is an archive built from a board)
                if( module->IsFlipped() )
                    module->Flip( module->GetPosition(), m_frame->Settings().m_FlipLeftRight );

                module->SetOrientation( 0 );
                module->SetPosition( wxPoint( cursorPos.x, cursorPos.y ) );

                commit.Add( module );
                m_toolMgr->RunAction( PCB_ACTIONS::selectItem, true, module );
                controls->SetCursorPosition( cursorPos, false );
            }
            else
            {
                m_toolMgr->RunAction( PCB_ACTIONS::selectionClear, true );
                commit.Push( _( "Place a module" ) );
                module = NULL;  // to indicate that there is no module that we currently modify
            }
        }

        else if( evt->IsClick( BUT_RIGHT ) )
        {
            m_menu.ShowContextMenu(  selection()  );
        }

        else if( module && ( evt->IsMotion() || evt->IsAction( &ACTIONS::refreshPreview ) ) )
        {
            module->SetPosition( wxPoint( cursorPos.x, cursorPos.y ) );
            selection().SetReferencePoint( cursorPos );
            getView()->Update( & selection()  );
        }

        else if( module && evt->IsAction( &PCB_ACTIONS::properties ) )
        {
            // Calling 'Properties' action clears the selection, so we need to restore it
            reselect = true;
        }

        else
            evt->SetPassEvent();

        // Enable autopanning and cursor capture only when there is a module to be placed
        controls->SetAutoPan( !!module );
        controls->CaptureCursor( !!module );
    }

    return 0;
}


int PCB_EDITOR_CONTROL::ToggleLockSelected( const TOOL_EVENT& aEvent )
{
    return modifyLockSelected( TOGGLE );
}


int PCB_EDITOR_CONTROL::LockSelected( const TOOL_EVENT& aEvent )
{
    return modifyLockSelected( ON );
}


int PCB_EDITOR_CONTROL::UnlockSelected( const TOOL_EVENT& aEvent )
{
    return modifyLockSelected( OFF );
}


int PCB_EDITOR_CONTROL::modifyLockSelected( MODIFY_MODE aMode )
{
    SELECTION_TOOL*         selTool = m_toolMgr->GetTool<SELECTION_TOOL>();
    const PCBNEW_SELECTION& selection = selTool->GetSelection();
    BOARD_COMMIT            commit( m_frame );

    if( selection.Empty() )
        m_toolMgr->RunAction( PCB_ACTIONS::selectionCursor, true );

    bool modified = false;

    for( EDA_ITEM* item : selection )
    {
        BOARD_ITEM* board_item = static_cast<BOARD_ITEM*>( item );
        bool        prevState = board_item->IsLocked();

        commit.Modify( board_item );

        switch( aMode )
        {
        case ON:     board_item->SetLocked( true );       break;
        case OFF:    board_item->SetLocked( false );      break;
        case TOGGLE: board_item->SetLocked( !prevState ); break;
        }

        // Check if we really modified an item
        if( !modified && prevState != board_item->IsLocked() )
            modified = true;
    }

    if( modified )
    {
        switch( aMode )
        {
        case ON:     commit.Push( _( "Lock" ) );           break;
        case OFF:    commit.Push( _( "Unlock" ) );         break;
        case TOGGLE: commit.Push( _( "Toggle Locking" ) ); break;
        }

        m_toolMgr->PostEvent( EVENTS::SelectedItemsModified );
        m_frame->OnModify();
    }

    return 0;
}


int PCB_EDITOR_CONTROL::GroupSelected( const TOOL_EVENT& aEvent )
{
    SELECTION_TOOL*         selTool   = m_toolMgr->GetTool<SELECTION_TOOL>();
    const PCBNEW_SELECTION& selection = selTool->GetSelection();
    BOARD*                  board     = getModel<BOARD>();
    BOARD_COMMIT            commit( m_frame );

    if( selection.Empty() )
        m_toolMgr->RunAction( PCB_ACTIONS::selectionCursor, true );
    // why don't we have to update the selection after selectionCursor action?

    PCB_GROUP* group = new PCB_GROUP( board );

    for( EDA_ITEM* item : selection )
        group->AddItem( static_cast<BOARD_ITEM*>( item ) );


    commit.Add( group );
    commit.Push( _( "GroupCreate" ) );
    wxString check = board->GroupsSanityCheck();
    wxCHECK_MSG( check == wxEmptyString, 0,wxT( "Group create resulted in inconsistent state: " ) + check );

    selTool->ClearSelection();
    selTool->select( group );

    // Should I call PostEvent and onModify() ?
    m_toolMgr->PostEvent( EVENTS::SelectedItemsModified );
    m_frame->OnModify();

    return 0;
}


int PCB_EDITOR_CONTROL::GroupMergeSelected( const TOOL_EVENT& aEvent )
{
    SELECTION_TOOL*         selTool   = m_toolMgr->GetTool<SELECTION_TOOL>();
    const PCBNEW_SELECTION& selection = selTool->GetSelection();
    BOARD*                  board     = getModel<BOARD>();
    BOARD_COMMIT            commit( m_frame );

    if( selection.Empty() )
        m_toolMgr->RunAction( PCB_ACTIONS::selectionCursor, true );
    // why don't we have to update the selection after selectionCursor action?

    PCB_GROUP* firstGroup = NULL;

    for( EDA_ITEM* item : selection )
    {
        BOARD_ITEM* board_item = static_cast<BOARD_ITEM*>( item );

        if( firstGroup == NULL && board_item->Type() == PCB_GROUP_T )
        {
            firstGroup = static_cast<PCB_GROUP*>( board_item );
            break;
        }
    }
    // The group submenu update() call only enabled merge if there was a group
    // in the selection.
    wxCHECK_MSG( firstGroup != NULL, 0, "Group not found in selection though selection was checked" );

    commit.Modify( firstGroup );

    for( EDA_ITEM* item : selection )
    {
        BOARD_ITEM* board_item = static_cast<BOARD_ITEM*>( item );

        if( board_item != firstGroup )
            firstGroup->AddItem( board_item );
    }

    commit.Push( "GroupMerge" );
    wxString check = board->GroupsSanityCheck();
    wxCHECK_MSG( check == wxEmptyString, 0, wxT( "Group merge resulted in inconsistent state: " ) + check );

    selTool->ClearSelection();
    selTool->select( firstGroup );

    // Should I call PostEvent and onModify() ?
    m_toolMgr->PostEvent( EVENTS::SelectedItemsModified );
    m_frame->OnModify();

    return 0;
}


int PCB_EDITOR_CONTROL::UngroupSelected( const TOOL_EVENT& aEvent )
{
    SELECTION_TOOL*         selTool   = m_toolMgr->GetTool<SELECTION_TOOL>();
    const PCBNEW_SELECTION& selection = selTool->GetSelection();
    BOARD*                  board     = getModel<BOARD>();
    BOARD_COMMIT            commit( m_frame );
    std::unordered_set<BOARD_ITEM*> ungroupedItems;

    if( selection.Empty() )
        m_toolMgr->RunAction( PCB_ACTIONS::selectionCursor, true );
    // why don't we have to update the selection after selectionCursor action?


    for( EDA_ITEM* item : selection )
    {
        BOARD_ITEM* board_item = static_cast<BOARD_ITEM*>( item );

        wxCHECK_MSG( board_item->Type() == PCB_GROUP_T, 0,
                     "Selection for ungroup should only have groups in it - was checked." );

        commit.Remove( board_item );

        for( BOARD_ITEM* bItem : static_cast<PCB_GROUP*>( board_item )->GetItems() )
        {
            ungroupedItems.insert( bItem );
        }
    }

    commit.Push( "GroupUngroup" );
    wxString check = board->GroupsSanityCheck();
    wxCHECK_MSG( check == wxEmptyString, 0, wxT( "Group merge resulted in inconsistent state: " ) + check );

    selTool->ClearSelection();
    for( BOARD_ITEM* item : ungroupedItems )
    {
        // commit.Remove() on the group recursively removed children from the view.
        // Add them back to the view
        //getView()->Add( item );

        selTool->select( item );
    }

    // Should I call PostEvent and onModify() ?
    m_toolMgr->PostEvent( EVENTS::SelectedItemsModified );
    m_frame->OnModify();

    return 0;
}


int PCB_EDITOR_CONTROL::GroupRemoveItemsSelected( const TOOL_EVENT& aEvent )
{
    SELECTION_TOOL*         selTool   = m_toolMgr->GetTool<SELECTION_TOOL>();
    const PCBNEW_SELECTION& selection = selTool->GetSelection();
    BOARD*                  board     = getModel<BOARD>();
    BOARD_COMMIT            commit( m_frame );

    if( selection.Empty() )
        m_toolMgr->RunAction( PCB_ACTIONS::selectionCursor, true );
    // why don't we have to update the selection after selectionCursor action?

    board->GroupRemoveItems( selection, &commit );

    commit.Push( "GroupRemoveItems" );
    wxString check = board->GroupsSanityCheck();
    wxCHECK_MSG( check == wxEmptyString, 0, wxT( "Group removeItems resulted in inconsistent state: " ) + check );

    // Should I call PostEvent and onModify() ?
    m_toolMgr->PostEvent( EVENTS::SelectedItemsModified );
    m_frame->OnModify();

    return 0;
}


int PCB_EDITOR_CONTROL::GroupFlattenSelected( const TOOL_EVENT& aEvent )
{
    SELECTION_TOOL*         selTool   = m_toolMgr->GetTool<SELECTION_TOOL>();
    const PCBNEW_SELECTION& selection = selTool->GetSelection();
    BOARD*                  board     = getModel<BOARD>();
    BOARD_COMMIT            commit( m_frame );
    const PCBNEW_SELECTION  origGroups = selTool->GetSelection();
    // These items were moved up to the top-level group that need to be readded to
    // the view.  That's becuase commit.Remove(group) recursively removed them from
    // the view.
    //std::unordered_set<BOARD_ITEM*> movedItems;

    if( selection.Empty() )
        m_toolMgr->RunAction( PCB_ACTIONS::selectionCursor, true );
    // why don't we have to update the selection after selectionCursor action?

    for( EDA_ITEM* item : selection )
    {
        BOARD_ITEM* board_item = static_cast<BOARD_ITEM*>( item );
        wxCHECK_MSG( board_item->Type() == PCB_GROUP_T, 0,
                     "Selection for ungroup should only have groups in it - was checked." );
        std::queue<PCB_GROUP*> groupsToFlatten;
        groupsToFlatten.push( static_cast<PCB_GROUP*>( board_item ) );
        PCB_GROUP* topGroup = groupsToFlatten.front();
        commit.Modify( topGroup );
        std::unordered_set<BOARD_ITEM*> topSubgroupsToRemove;

        while( !groupsToFlatten.empty() )
        {
            PCB_GROUP* grp = groupsToFlatten.front();
            groupsToFlatten.pop();

            for( BOARD_ITEM* grpItem : grp->GetItems() )
            {
                if( grpItem->Type() == PCB_GROUP_T )
                {
                    groupsToFlatten.push( static_cast<PCB_GROUP*>( grpItem ) );
                    commit.Remove( grpItem );
                    if( grp == topGroup )
                        topSubgroupsToRemove.insert( grpItem );
                }
                else
                {
                    if( grp != topGroup )
                    {
                        wxCHECK( topGroup->AddItem( grpItem ), 0 );
                        //movedItems.insert( grpItem );
                    }
                }
            }
        }

        for( BOARD_ITEM* group : topSubgroupsToRemove )
        {
            topGroup->RemoveItem( group );
        }
    }

    commit.Push( "GroupFlatten" );
    wxString check = board->GroupsSanityCheck();
    wxCHECK_MSG( check == wxEmptyString, 0, wxT( "Group flatten resulted in inconsistent state: " ) + check );

    // Removing subgroups deselects the items in them. So reselect everything no that it's flattened.
    selTool->ClearSelection();
    for( EDA_ITEM* item : origGroups )
        selTool->select( static_cast<BOARD_ITEM*>( item ) );

    // Should I call PostEvent and onModify() ?
    m_toolMgr->PostEvent( EVENTS::SelectedItemsModified );
    m_frame->OnModify();

    return 0;
}


int PCB_EDITOR_CONTROL::GroupEnterSelected( const TOOL_EVENT& aEvent )
{
    SELECTION_TOOL*         selTool   = m_toolMgr->GetTool<SELECTION_TOOL>();
    const PCBNEW_SELECTION& selection = selTool->GetSelection();

    if( selection.GetSize() == 1 && selection[0]->Type() == PCB_GROUP_T )
        selTool->EnterGroup();

    return 0;
}


int PCB_EDITOR_CONTROL::GroupLeave( const TOOL_EVENT& aEvent )
{
    m_toolMgr->GetTool<SELECTION_TOOL>()->ExitGroup( true /* Select the group */ );
    return 0;
}


int PCB_EDITOR_CONTROL::PlaceTarget( const TOOL_EVENT& aEvent )
{
    KIGFX::VIEW* view = getView();
    KIGFX::VIEW_CONTROLS* controls = getViewControls();
    BOARD* board = getModel<BOARD>();
    PCB_TARGET* target = new PCB_TARGET( board );

    // Init the new item attributes
    target->SetLayer( Edge_Cuts );
    target->SetWidth( board->GetDesignSettings().GetLineThickness( Edge_Cuts ) );
    target->SetSize( Millimeter2iu( 5 ) );
    VECTOR2I cursorPos = controls->GetCursorPosition();
    target->SetPosition( wxPoint( cursorPos.x, cursorPos.y ) );

    // Add a VIEW_GROUP that serves as a preview for the new item
    KIGFX::VIEW_GROUP preview( view );
    preview.Add( target );
    view->Add( &preview );

    m_toolMgr->RunAction( PCB_ACTIONS::selectionClear, true );

    std::string tool = aEvent.GetCommandStr().get();
    m_frame->PushTool( tool );
    Activate();

    // Main loop: keep receiving events
    while( TOOL_EVENT* evt = Wait() )
    {
        frame()->GetCanvas()->SetCurrentCursor( wxCURSOR_ARROW );
        cursorPos = controls->GetCursorPosition( !evt->Modifier( MD_ALT ) );

        if( evt->IsCancelInteractive() )
        {
            frame()->PopTool( tool );
            break;
        }

        else if( evt->IsActivate() )
        {
            if( evt->IsMoveTool() )
            {
                // leave ourselves on the stack so we come back after the move
                break;
            }
            else
            {
                frame()->PopTool( tool );
                break;
            }
        }

        else if( evt->IsAction( &PCB_ACTIONS::incWidth ) )
        {
            target->SetWidth( target->GetWidth() + WIDTH_STEP );
            view->Update( &preview );
        }

        else if( evt->IsAction( &PCB_ACTIONS::decWidth ) )
        {
            int width = target->GetWidth();

            if( width > WIDTH_STEP )
            {
                target->SetWidth( width - WIDTH_STEP );
                view->Update( &preview );
            }
        }

        else if( evt->IsClick( BUT_LEFT ) )
        {
            assert( target->GetSize() > 0 );
            assert( target->GetWidth() > 0 );

            BOARD_COMMIT commit( m_frame );
            commit.Add( target );
            commit.Push( "Place a layer alignment target" );

            preview.Remove( target );

            // Create next PCB_TARGET
            target = new PCB_TARGET( *target );
            preview.Add( target );
        }

        else if( evt->IsClick( BUT_RIGHT ) )
        {
            m_menu.ShowContextMenu( selection() );
        }

        else if( evt->IsMotion() )
        {
            target->SetPosition( wxPoint( cursorPos.x, cursorPos.y ) );
            view->Update( &preview );
        }

        else
            evt->SetPassEvent();
    }

    preview.Clear();
    delete target;
    view->Remove( &preview );
    return 0;
}


static bool mergeZones( BOARD_COMMIT& aCommit, std::vector<ZONE_CONTAINER *>& aOriginZones,
        std::vector<ZONE_CONTAINER *>& aMergedZones )
{
    for( unsigned int i = 1; i < aOriginZones.size(); i++ )
    {
        aOriginZones[0]->Outline()->BooleanAdd( *aOriginZones[i]->Outline(),
                                                SHAPE_POLY_SET::PM_FAST );
    }

    aOriginZones[0]->Outline()->Simplify( SHAPE_POLY_SET::PM_FAST );

    // We should have one polygon with hole
    // We can have 2 polygons with hole, if the 2 initial polygons have only one common corner
    // and therefore cannot be merged (they are dectected as intersecting)
    // but we should never have more than 2 polys
    if( aOriginZones[0]->Outline()->OutlineCount() > 1 )
    {
        wxLogMessage( "BOARD::CombineAreas error: more than 2 polys after merging" );
        return false;
    }

    for( unsigned int i = 1; i < aOriginZones.size(); i++ )
    {
        aCommit.Remove( aOriginZones[i] );
    }

    aCommit.Modify( aOriginZones[0] );
    aMergedZones.push_back( aOriginZones[0] );

    aOriginZones[0]->SetLocalFlags( 1 );
    aOriginZones[0]->HatchBorder();
    aOriginZones[0]->CacheTriangulation();

    return true;
}


int PCB_EDITOR_CONTROL::ZoneMerge( const TOOL_EVENT& aEvent )
{
    const PCBNEW_SELECTION& selection = m_toolMgr->GetTool<SELECTION_TOOL>()->GetSelection();
    BOARD*                  board = getModel<BOARD>();
    BOARD_COMMIT            commit( m_frame );

    if( selection.Size() < 2 )
        return 0;

    int netcode = -1;

    ZONE_CONTAINER* firstZone = nullptr;
    std::vector<ZONE_CONTAINER*> toMerge, merged;

    for( auto item : selection )
    {
        auto curr_area = dynamic_cast<ZONE_CONTAINER*>( item );

        if( !curr_area )
            continue;

        if( !firstZone )
            firstZone = curr_area;

        netcode = curr_area->GetNetCode();

        if( firstZone->GetNetCode() != netcode )
            continue;

        if( curr_area->GetPriority() != firstZone->GetPriority() )
            continue;

        if( curr_area->GetIsRuleArea() != firstZone->GetIsRuleArea() )
            continue;

        if( curr_area->GetLayer() != firstZone->GetLayer() )
            continue;

        if( !board->TestAreaIntersection( curr_area, firstZone ) )
            continue;

        toMerge.push_back( curr_area );
    }

    m_toolMgr->RunAction( PCB_ACTIONS::selectionClear, true );

    if( mergeZones( commit, toMerge, merged ) )
    {
        commit.Push( "Merge zones" );

        for( auto item : merged )
            m_toolMgr->RunAction( PCB_ACTIONS::selectItem, true, item );
    }

    return 0;
}


int PCB_EDITOR_CONTROL::ZoneDuplicate( const TOOL_EVENT& aEvent )
{
    SELECTION_TOOL*  selTool = m_toolMgr->GetTool<SELECTION_TOOL>();
    const SELECTION& selection = selTool->GetSelection();

    // because this pops up the zone editor, it would be confusing to handle multiple zones,
    // so just handle single selections containing exactly one zone
    if( selection.Size() != 1 )
        return 0;

    auto oldZone = dyn_cast<ZONE_CONTAINER*>( selection[0] );

    if( !oldZone )
        return 0;

    ZONE_SETTINGS zoneSettings;
    zoneSettings << *oldZone;
    int dialogResult;

    if( oldZone->GetIsRuleArea() )
        dialogResult = InvokeRuleAreaEditor( m_frame, &zoneSettings );
    else if( oldZone->IsOnCopperLayer() )
        dialogResult = InvokeCopperZonesEditor( m_frame, &zoneSettings );
    else
        dialogResult = InvokeNonCopperZonesEditor( m_frame, &zoneSettings );

    if( dialogResult != wxID_OK )
        return 0;

    // duplicate the zone
    BOARD_COMMIT commit( m_frame );

    auto newZone = std::make_unique<ZONE_CONTAINER>( *oldZone );
    newZone->ClearSelected();
    newZone->UnFill();
    zoneSettings.ExportSetting( *newZone );

    // If the new zone is on the same layer(s) as the the initial zone,
    // offset it a bit so it can more easily be picked.
    if( oldZone->GetIsRuleArea() && ( oldZone->GetLayerSet() == zoneSettings.m_Layers ) )
        newZone->Move( wxPoint( IU_PER_MM, IU_PER_MM ) );
    else if( !oldZone->GetIsRuleArea() && zoneSettings.m_Layers.test( oldZone->GetLayer() ) )
        newZone->Move( wxPoint( IU_PER_MM, IU_PER_MM ) );

    commit.Add( newZone.release() );
    commit.Push( _( "Duplicate zone" ) );

    return 0;
}


int PCB_EDITOR_CONTROL::EditFpInFpEditor( const TOOL_EVENT& aEvent )
{
    SELECTION_TOOL*  selTool = m_toolMgr->GetTool<SELECTION_TOOL>();
    const SELECTION& selection = selTool->RequestSelection( EDIT_TOOL::FootprintFilter );

    if( selection.Empty() )
        return 0;

    MODULE* mod = selection.FirstOfKind<MODULE>();

    if( !mod )
        return 0;

    PCB_BASE_EDIT_FRAME* editFrame = getEditFrame<PCB_BASE_EDIT_FRAME>();

    auto editor = (FOOTPRINT_EDIT_FRAME*) editFrame->Kiway().Player( FRAME_FOOTPRINT_EDITOR, true );

    editor->Load_Module_From_BOARD( mod );

    editor->Show( true );
    editor->Raise();        // Iconize( false );

    if( selection.IsHover() )
        m_toolMgr->RunAction( PCB_ACTIONS::selectionClear, true );

    return 0;
}


void PCB_EDITOR_CONTROL::DoSetDrillOrigin( KIGFX::VIEW* aView, PCB_BASE_FRAME* aFrame,
                                           EDA_ITEM* originViewItem, const VECTOR2D& aPosition )
{
    aFrame->GetDesignSettings().m_AuxOrigin = (wxPoint) aPosition;
    originViewItem->SetPosition( (wxPoint) aPosition );
    aView->MarkDirty();
    aFrame->OnModify();
}


int PCB_EDITOR_CONTROL::DrillOrigin( const TOOL_EVENT& aEvent )
{
    std::string         tool = aEvent.GetCommandStr().get();
    PCBNEW_PICKER_TOOL* picker = m_toolMgr->GetTool<PCBNEW_PICKER_TOOL>();

    // Deactivate other tools; particularly important if another PICKER is currently running
    Activate();

    picker->SetClickHandler(
        [this] ( const VECTOR2D& pt ) -> bool
        {
            m_frame->SaveCopyInUndoList( m_placeOrigin.get(), UNDO_REDO::DRILLORIGIN );
            DoSetDrillOrigin( getView(), m_frame, m_placeOrigin.get(), pt );
            return false;   // drill origin is a one-shot; don't continue with tool
        } );

    m_toolMgr->RunAction( ACTIONS::pickerTool, true, &tool );

    return 0;
}


int PCB_EDITOR_CONTROL::FlipPcbView( const TOOL_EVENT& aEvent )
{
    view()->SetMirror( !view()->IsMirroredX(), false );
    view()->RecacheAllItems();
    frame()->Refresh();
    frame()->OnDisplayOptionsChanged();
    return 0;
}


void PCB_EDITOR_CONTROL::setTransitions()
{
    Go( &PCB_EDITOR_CONTROL::New,                      ACTIONS::doNew.MakeEvent() );
    Go( &PCB_EDITOR_CONTROL::Open,                     ACTIONS::open.MakeEvent() );
    Go( &PCB_EDITOR_CONTROL::Save,                     ACTIONS::save.MakeEvent() );
    Go( &PCB_EDITOR_CONTROL::SaveAs,                   ACTIONS::saveAs.MakeEvent() );
    Go( &PCB_EDITOR_CONTROL::SaveCopyAs,               ACTIONS::saveCopyAs.MakeEvent() );
    Go( &PCB_EDITOR_CONTROL::PageSettings,             ACTIONS::pageSettings.MakeEvent() );
    Go( &PCB_EDITOR_CONTROL::Plot,                     ACTIONS::plot.MakeEvent() );

    Go( &PCB_EDITOR_CONTROL::BoardSetup,               PCB_ACTIONS::boardSetup.MakeEvent() );
    Go( &PCB_EDITOR_CONTROL::ImportNetlist,            PCB_ACTIONS::importNetlist.MakeEvent() );
    Go( &PCB_EDITOR_CONTROL::ImportSpecctraSession,    PCB_ACTIONS::importSpecctraSession.MakeEvent() );
    Go( &PCB_EDITOR_CONTROL::ExportSpecctraDSN,        PCB_ACTIONS::exportSpecctraDSN.MakeEvent() );
    Go( &PCB_EDITOR_CONTROL::GenerateDrillFiles,       PCB_ACTIONS::generateDrillFiles.MakeEvent() );
    Go( &PCB_EDITOR_CONTROL::GenerateFabFiles,         PCB_ACTIONS::generateGerbers.MakeEvent() );
    Go( &PCB_EDITOR_CONTROL::GeneratePosFile,          PCB_ACTIONS::generatePosFile.MakeEvent() );
    Go( &PCB_EDITOR_CONTROL::GenerateFabFiles,         PCB_ACTIONS::generateReportFile.MakeEvent() );
    Go( &PCB_EDITOR_CONTROL::GenerateFabFiles,         PCB_ACTIONS::generateD356File.MakeEvent() );
    Go( &PCB_EDITOR_CONTROL::GenerateFabFiles,         PCB_ACTIONS::generateBOM.MakeEvent() );

    // Track & via size control
    Go( &PCB_EDITOR_CONTROL::TrackWidthInc,            PCB_ACTIONS::trackWidthInc.MakeEvent() );
    Go( &PCB_EDITOR_CONTROL::TrackWidthDec,            PCB_ACTIONS::trackWidthDec.MakeEvent() );
    Go( &PCB_EDITOR_CONTROL::ViaSizeInc,               PCB_ACTIONS::viaSizeInc.MakeEvent() );
    Go( &PCB_EDITOR_CONTROL::ViaSizeDec,               PCB_ACTIONS::viaSizeDec.MakeEvent() );

    // Zone actions
    Go( &PCB_EDITOR_CONTROL::ZoneMerge,                PCB_ACTIONS::zoneMerge.MakeEvent() );
    Go( &PCB_EDITOR_CONTROL::ZoneDuplicate,            PCB_ACTIONS::zoneDuplicate.MakeEvent() );

    // Placing tools
    Go( &PCB_EDITOR_CONTROL::PlaceTarget,              PCB_ACTIONS::placeTarget.MakeEvent() );
    Go( &PCB_EDITOR_CONTROL::PlaceModule,              PCB_ACTIONS::placeModule.MakeEvent() );
    Go( &PCB_EDITOR_CONTROL::DrillOrigin,              PCB_ACTIONS::drillOrigin.MakeEvent() );

    Go( &PCB_EDITOR_CONTROL::EditFpInFpEditor,         PCB_ACTIONS::editFpInFpEditor.MakeEvent() );

    // Other
    Go( &PCB_EDITOR_CONTROL::ToggleLockSelected,       PCB_ACTIONS::toggleLock.MakeEvent() );
    Go( &PCB_EDITOR_CONTROL::LockSelected,             PCB_ACTIONS::lock.MakeEvent() );
    Go( &PCB_EDITOR_CONTROL::UnlockSelected,           PCB_ACTIONS::unlock.MakeEvent() );
    Go( &PCB_EDITOR_CONTROL::GroupSelected,            PCB_ACTIONS::groupCreate.MakeEvent() );
    Go( &PCB_EDITOR_CONTROL::GroupMergeSelected,       PCB_ACTIONS::groupMerge.MakeEvent() );
    Go( &PCB_EDITOR_CONTROL::UngroupSelected,          PCB_ACTIONS::groupUngroup.MakeEvent() );
    Go( &PCB_EDITOR_CONTROL::GroupRemoveItemsSelected, PCB_ACTIONS::groupRemoveItems.MakeEvent() );
    Go( &PCB_EDITOR_CONTROL::GroupFlattenSelected,     PCB_ACTIONS::groupFlatten.MakeEvent() );
    Go( &PCB_EDITOR_CONTROL::GroupEnterSelected,       PCB_ACTIONS::groupEnter.MakeEvent() );
    Go( &PCB_EDITOR_CONTROL::GroupLeave,               PCB_ACTIONS::groupLeave.MakeEvent() );

    Go( &PCB_EDITOR_CONTROL::UpdatePCBFromSchematic,   ACTIONS::updatePcbFromSchematic.MakeEvent() );
    Go( &PCB_EDITOR_CONTROL::UpdateSchematicFromPCB,   ACTIONS::updateSchematicFromPcb.MakeEvent() );
    Go( &PCB_EDITOR_CONTROL::ShowEeschema,             PCB_ACTIONS::showEeschema.MakeEvent() );
    Go( &PCB_EDITOR_CONTROL::ToggleLayersManager,      PCB_ACTIONS::showLayersManager.MakeEvent() );
    Go( &PCB_EDITOR_CONTROL::ToggleMicrowaveToolbar,   PCB_ACTIONS::showMicrowaveToolbar.MakeEvent() );
    Go( &PCB_EDITOR_CONTROL::TogglePythonConsole,      PCB_ACTIONS::showPythonConsole.MakeEvent() );
    Go( &PCB_EDITOR_CONTROL::FlipPcbView,              PCB_ACTIONS::flipBoard.MakeEvent() );
    Go( &PCB_EDITOR_CONTROL::RepairBoard,              PCB_ACTIONS::repairBoard.MakeEvent() );
}


const int PCB_EDITOR_CONTROL::WIDTH_STEP = 100000;
