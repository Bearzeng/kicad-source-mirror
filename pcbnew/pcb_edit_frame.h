/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2010 Jean-Pierre Charras, jp.charras@wanadoo.fr
 * Copyright (C) 1992-2019 KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef  WXPCB_STRUCT_H_
#define  WXPCB_STRUCT_H_

#include <unordered_map>
#include <map>
#include "pcb_base_edit_frame.h"
#include "undo_redo_container.h"
#include "zones.h"
#include <mail_type.h>
#include <map>
#include <unordered_map>

/*  Forward declarations of classes. */
class ACTION_PLUGIN;
class PCB_SCREEN;
class BOARD;
class BOARD_COMMIT;
class BOARD_ITEM_CONTAINER;
class MODULE;
class TRACK;
class VIA;
class D_PAD;
class PCB_TARGET;
class PCB_GROUP;
class DIMENSION;
class DRC;
class DIALOG_PLOT;
class ZONE_CONTAINER;
class GENERAL_COLLECTOR;
class GENERAL_COLLECTORS_GUIDE;
class SELECTION;
class MARKER_PCB;
class BOARD_ITEM;
class PCB_LAYER_BOX_SELECTOR;
class NETLIST;
class REPORTER;
struct PARSE_ERROR;
class IO_ERROR;
class FP_LIB_TABLE;
class BOARD_NETLIST_UPDATER;
class ACTION_MENU;
enum LAST_PATH_TYPE : unsigned int;

namespace PCB { struct IFACE; }     // KIFACE_I is in pcbnew.cpp

/**
 * PCB_EDIT_FRAME
 * is the main frame for Pcbnew.
 *
 * See also class PCB_BASE_FRAME(): Basic class for Pcbnew and GerbView.
 */


class PCB_EDIT_FRAME : public PCB_BASE_EDIT_FRAME
{
    friend struct PCB::IFACE;
    friend class APPEARANCE_CONTROLS;

protected:

    /**
     * Store the previous layer toolbar icon state information
     */
    struct LAYER_TOOLBAR_ICON_VALUES
    {
        int     previous_requested_scale;
        COLOR4D previous_active_layer_color;
        COLOR4D previous_Route_Layer_TOP_color;
        COLOR4D previous_Route_Layer_BOTTOM_color;
        COLOR4D previous_via_color;
        COLOR4D previous_background_color;

        LAYER_TOOLBAR_ICON_VALUES()
                : previous_requested_scale( 0 ),
                  previous_active_layer_color( COLOR4D::UNSPECIFIED ),
                  previous_Route_Layer_TOP_color( COLOR4D::UNSPECIFIED ),
                  previous_Route_Layer_BOTTOM_color( COLOR4D::UNSPECIFIED ),
                  previous_via_color( COLOR4D::UNSPECIFIED ),
                  previous_background_color( COLOR4D::UNSPECIFIED )
        {
        }
    };

    LAYER_TOOLBAR_ICON_VALUES m_prevIconVal;

    // The Tool Framework initalization
    void setupTools();
    void setupUIConditions() override;

    /**
     * switches currently used canvas (Cairo / OpenGL).
     * It also reinit the layers manager that slightly changes with canvases
     */
    void SwitchCanvas( EDA_DRAW_PANEL_GAL::GAL_TYPE aCanvasType ) override;

#if defined(KICAD_SCRIPTING) && defined(KICAD_SCRIPTING_ACTION_MENU)
    /**
     * Function buildActionPluginMenus
     * Fill action menu with all registered action plugins
     */
    void buildActionPluginMenus( ACTION_MENU* aActionMenu );

    /**
     * Function AddActionPluginTools
     * Append action plugin buttons to main toolbar
     */
    void AddActionPluginTools();

	/**
	 * Function RunActionPlugin
	 * Executes action plugin's Run() method and updates undo buffer
	 * @param aActionPlugin action plugin
	 */
	void RunActionPlugin( ACTION_PLUGIN* aActionPlugin );

    /**
     * Function OnActionPluginMenu
     * Launched by the menu when an action is called
     * @param aEvent sent by wx
     */
    void OnActionPluginMenu( wxCommandEvent& aEvent);

    /**
     * Function OnActionPluginButton
     * Launched by the button when an action is called
     * @param aEvent sent by wx
     */
    void OnActionPluginButton( wxCommandEvent& aEvent );

    /**
     * Function OnActionPluginRefresh
     * Refresh plugin list (reload Python plugins)
     * @param aEvent sent by wx
     */
    void OnActionPluginRefresh( wxCommandEvent& aEvent)
    {
       PythonPluginsReload();
    }

    /**
     * Function OnActionPluginRefresh
     * Refresh plugin list (reload Python plugins)
     * @param aEvent sent by wx
     */
    void OnActionPluginShowFolder( wxCommandEvent& aEvent)
    {
       PythonPluginsShowFolder();
    }
#endif

    /** Has meaning only if KICAD_SCRIPTING_WXPYTHON option is
     * not defined
     * @return the frame name identifier for the python console frame
     */
    static const wxChar * pythonConsoleNameId()
    {
        return wxT( "PythonConsole" );
    }

    /**
     * @return a pointer to the python console frame, or NULL if not exist
     */
    static wxWindow * findPythonConsole()
    {
       return FindWindowByName( pythonConsoleNameId() );
    }

    /**
     * Updates the state of the GUI after a new board is loaded or created
     */
    void onBoardLoaded();

    /**
     * Function doAutoSave
     * performs auto save when the board has been modified and not saved within the
     * auto save interval.
     *
     * @return true if the auto save was successful.
     */
    bool doAutoSave() override;

    /**
     * Function isautoSaveRequired
     * returns true if the board has been modified.
     */
    bool isAutoSaveRequired() const override;

    /**
     * Load the given filename but sets the path to the current project path.
     * @param full filepath of file to be imported.
     * @param aFileType PCB_FILE_T value for filetype
     */
    bool importFile( const wxString& aFileName, int aFileType );

    /**
     * Use the existing edge_cut line thicknesses to infer the edge clearace.
     */
    int inferLegacyEdgeClearance( BOARD* aBoard );

    /**
     * Rematch orphaned zones and vias to schematic nets.
     */
    bool fixEagleNets( const std::unordered_map<wxString, wxString>& aRemap );

    bool canCloseWindow( wxCloseEvent& aCloseEvent ) override;
    void doCloseWindow() override;

    // protected so that PCB::IFACE::CreateWindow() is the only factory.
    PCB_EDIT_FRAME( KIWAY* aKiway, wxWindow* aParent );

    void onSize( wxSizeEvent& aEvent );

public:
    PCB_LAYER_BOX_SELECTOR* m_SelLayerBox;  // a combo box to display and select active layer
    wxChoice* m_SelTrackWidthBox;           // a choice box to display and select current track width
    wxChoice* m_SelViaSizeBox;              // a choice box to display and select current via diameter

    bool m_show_layer_manager_tools;

    bool m_ZoneFillsDirty;                  // Board has been modified since last zone fill.

    virtual ~PCB_EDIT_FRAME();

    /**
     * Function loadFootprints
     * loads the footprints for each #COMPONENT in \a aNetlist from the list of libraries.
     *
     * @param aNetlist is the netlist of components to load the footprints into.
     * @param aReporter is the #REPORTER object to report to.
     * @throw IO_ERROR if an I/O error occurs or a #PARSE_ERROR if a file parsing error
     *           occurs while reading footprint library files.
     */
    void LoadFootprints( NETLIST& aNetlist, REPORTER& aReporter );

    void OnQuit( wxCommandEvent& event );

    /**
     * Get if the current board has been modified but not saved.
     *
     * @return true if the any changes have not been saved
     */
    bool IsContentModified() override;

    /**
     * Reload the Python plugins if they are newer than the already loaded, and load new
     * plugins if any.
     * Do nothing if KICAD_SCRIPTING is not defined.
     */
    void PythonPluginsReload();

    /**
     * Open the plugins folder in the default system file browser.
     * Do nothing if KICAD_SCRIPTING is not defined.
     */
    void PythonPluginsShowFolder();

    /**
     * Synchronize the environment variables from KiCad's environment into the Python interpreter.
     * Do nothing if KICAD_SCRIPTING is not defined.
     */
    void PythonSyncEnvironmentVariables();

    /**
     * Synchronize the project name from KiCad's environment into the Python interpreter.
     * Do nothing if KICAD_SCRIPTING is not defined.
     */
    void PythonSyncProjectName();

    /**
     * Update the layer manager and other widgets from the board setup
     * (layer and items visibility, colors ...)
     */
    void UpdateUserInterface();

    /**
     * Execute a remote command send by Eeschema via a socket,
     * port KICAD_PCB_PORT_SERVICE_NUMBER (currently 4242)
     * this is a virtual function called by EDA_DRAW_FRAME::OnSockRequest().
     * @param cmdline = received command from socket
     */
    void ExecuteRemoteCommand( const char* cmdline ) override;

    void KiwayMailIn( KIWAY_EXPRESS& aEvent ) override;

    /**
     * Function ToPlotter
     * Open a dialog frame to create plot and drill files relative to the current board.
     */
    void ToPlotter( int aID );

    /**
     * Function SVG_Print
     * Shows the Export to SVG file dialog.
     */
    void ExportSVG( wxCommandEvent& event );

    // User interface update command event handlers.
    void OnUpdateLayerSelectBox( wxUpdateUIEvent& aEvent );
    bool LayerManagerShown();
    void OnUpdateSelectViaSize( wxUpdateUIEvent& aEvent );
    void OnUpdateSelectTrackWidth( wxUpdateUIEvent& aEvent );
    void OnUpdateSelectAutoWidth( wxUpdateUIEvent& aEvent );

    void RunEeschema();

    void UpdateTrackWidthSelectBox( wxChoice* aTrackWidthSelectBox, bool aEdit = true );
    void UpdateViaSizeSelectBox( wxChoice* aViaSizeSelectBox, bool aEdit = true );

    /**
     * Function GetGridColor() , virtual
     * @return the color of the grid
     */
    COLOR4D GetGridColor() override;

    /**
     * Function SetGridColor() , virtual
     * @param aColor = the new color of the grid
     */
    void SetGridColor( COLOR4D aColor ) override;

    // Configurations:
    void Process_Config( wxCommandEvent& event );

#if defined(KICAD_SCRIPTING) && defined(KICAD_SCRIPTING_ACTION_MENU)

    /**
     * Function GetActionPluginButtonVisible
     * Returns true if button visibility action plugin setting was set to true
     * or it is unset and plugin defaults to true.
     */
    bool GetActionPluginButtonVisible( const wxString& aPluginPath, bool aPluginDefault );

    /**
     * Function GetOrderedActionPlugins
     * Returns ordered list of plugins in sequence in which they should appear on toolbar or in settings
     */
    std::vector<ACTION_PLUGIN*> GetOrderedActionPlugins();

#endif

    /**
     * Function SaveProjectSettings
     * saves changes to the project settings to the project (.pro) file.
     */
    void SaveProjectSettings() override;

    /**
     * Load the current project's file configuration settings which are pertinent
     * to this PCB_EDIT_FRAME instance.
     *
     * @return always returns true.
     */
    bool LoadProjectSettings();

    void LoadSettings( APP_SETTINGS_BASE* aCfg ) override;

    void SaveSettings( APP_SETTINGS_BASE* aCfg ) override;

    /**
     * Get the last path for a particular type.
     * @return - Absolute path and file name of the last file successfully read.
     */
    wxString GetLastPath( LAST_PATH_TYPE aType );

    /**
     * Set the path of the last file successfully read.
     *
     * Note: the file path is converted to a path relative to the project file path.  If
     *       the path cannot be made relative, than m_lastNetListRead is set to and empty
     *       string.  This could happen when the net list file is on a different drive than
     *       the project file.  The advantage of relative paths is that is more likely to
     *       work when opening the same project from both Windows and Linux.
     *
     * @param aLastPath - The last file with full path successfully read.
     */
    void SetLastPath( LAST_PATH_TYPE aType, const wxString& aLastPath );

    /**
     * Scan existing markers and record data from any that are Excluded.
     */
    void RecordDRCExclusions();

    /**
     * Update markers to match recorded exclusions.
     */
    void ResolveDRCExclusions();

    void Process_Special_Functions( wxCommandEvent& event );
    void Tracks_and_Vias_Size_Event( wxCommandEvent& event );

    void ReCreateHToolbar() override;
    void ReCreateAuxiliaryToolbar() override;
    void ReCreateVToolbar() override;
    void ReCreateOptToolbar() override;
    void ReCreateMenuBar() override;

    /**
     * Re create the layer Box by clearing the old list, and building
     * le new one, from the new layers names and cole layers
     * @param aForceResizeToolbar = true to resize the parent toolbar
     * false if not needed (mainly in parent toolbar creation,
     * or when the layers names are not modified)
     */
    void ReCreateLayerBox( bool aForceResizeToolbar = true );


    /**
     * Function OnModify
     * must be called after a board change to set the modified flag.
     * <p>
     * Reloads the 3D view if required and calls the base PCB_BASE_FRAME::OnModify function
     * to update auxiliary information.
     * </p>
     */
    void OnModify() override;

    /**
     * Function SetActiveLayer
     * will change the currently active layer to \a aLayer and also
     * update the PCB_LAYER_WIDGET.
     */
    void SetActiveLayer( PCB_LAYER_ID aLayer ) override;

    APPEARANCE_CONTROLS* GetAppearancePanel() { return m_appearancePanel; }

    /**
     * Update the UI to reflect changes to the current layer's transparency.
     */
    void OnUpdateLayerAlpha( wxUpdateUIEvent& aEvent ) override;

    void OnDisplayOptionsChanged() override;

    /**
     * Function IsElementVisible
     * tests whether a given element category is visible. Keep this as an
     * inline function.
     * @param aElement is from the enum by the same name
     * @return bool - true if the element is visible.
     * @see enum GAL_LAYER_ID
     */
    bool IsElementVisible( GAL_LAYER_ID aElement ) const;

    /**
     * Function SetElementVisibility
     * changes the visibility of an element category
     * @param aElement is from the enum by the same name
     * @param aNewState The new visibility state of the element category
     * @see enum PCB_LAYER_ID
     */
    void SetElementVisibility( GAL_LAYER_ID aElement, bool aNewState );

    ///> @copydoc EDA_DRAW_FRAME::UseGalCanvas()
    void ActivateGalCanvas() override;

    /**
     * Function ShowBoardSetupDialog
     */
    void ShowBoardSetupDialog( const wxString& aInitialPage = wxEmptyString );

    /* toolbars update UI functions: */

    void PrepareLayerIndicator( bool aForceRebuild = false );

    void ToggleLayersManager();

    /**
     * Function DoGenFootprintsPositionFile
     * Creates an ascii footprint position file
     * @param aFullFileName = the full file name of the file to create
     * @param aUnitsMM = false to use inches, true to use mm in coordinates
     * @param aForceSmdItems = true to force all footprints with smd pads in list
     *                       = false to put only footprints with option "INSERT" in list
     * @param aTopSide true to list footprints on front (top) side,
     * @param aBottomSide true to list footprints on back (bottom) side,
     * if aTopSide and aTopSide are true, list footprints on both sides
     * @param aFormatCSV = true to use a comma separated file (CSV) format; defautl = false
     * @return the number of footprints found on aSide side,
     *    or -1 if the file could not be created
     */
    int DoGenFootprintsPositionFile( const wxString& aFullFileName, bool aUnitsMM,
                                     bool aForceSmdItems, bool aTopSide, bool aBottomSide, bool aFormatCSV = false );

    /**
     * Function GenFootprintsReport
     * Calls DoGenFootprintsReport to create a footprint reprot file
     * See DoGenFootprintsReport for file format
     */
    void GenFootprintsReport( wxCommandEvent& event );

    /**
     * Function DoGenFootprintsReport
     * Creates an ascii footprint report file giving some infos on footprints
     * and board outlines
     * @param aFullFilename = the full file name of the file to create
     * @param aUnitsMM = false to use inches, true to use mm in coordinates
     * @return true if OK, false if error
     */
    bool DoGenFootprintsReport( const wxString& aFullFilename, bool aUnitsMM );

    void GenD356File( wxCommandEvent& event );

    void OnFileHistory( wxCommandEvent& event );
    void OnClearFileHistory( wxCommandEvent& aEvent );

    /**
     * Function Files_io
     * @param event is the command event handler.
     * do nothing else than call Files_io_from_id with the
     * wxCommandEvent id
     */
    void Files_io( wxCommandEvent& event );

    /**
     * Function Files_io_from_id
     * Read and write board files
     * @param aId is an event ID ciming from file command events:
     * ID_LOAD_FILE
     * ID_MENU_RECOVER_BOARD_AUTOSAVE
     * ID_NEW_BOARD
     * ID_SAVE_BOARD
     * ID_COPY_BOARD_AS
     * ID_SAVE_BOARD_AS
     * Files_io_from_id prepare parameters and calls the specialized function
     */
    bool Files_io_from_id( int aId );

    /**
     * Function OpenProjectFiles    (was LoadOnePcbFile)
     * loads a KiCad board (.kicad_pcb) from \a aFileName.
     *
     * @param aFileSet - hold the BOARD file to load, a vector of one element.
     *
     * @param aCtl      - KICTL_ bits, one to indicate that an append of the board file
     *                      aFileName to the currently loaded file is desired.
     *                    @see #KIWAY_PLAYER for bit defines.
     *
     * @return bool - false if file load fails, otherwise true.
    bool LoadOnePcbFile( const wxString& aFileName, bool aAppend = false,
                         bool aForceFileDialog = false );
     */
    bool OpenProjectFiles( const std::vector<wxString>& aFileSet, int aCtl = 0 ) override;

    /**
     * Function SavePcbFile
     * writes the board data structures to \a a aFileName
     * Creates backup when requested and update flags (modified and saved flgs)
     *
     * @param aFileName The file name to write or wxEmptyString to prompt user for
     *                  file name.
     * @param addToHistory controsl whether or not to add the saved file to the recent file list
     * @param aChangeProject is true if the project should be changed to the new board filename
     * @return True if file was saved successfully.
     */
    bool SavePcbFile( const wxString& aFileName, bool addToHistory = true,
                      bool aChangeProject = true );

    /**
     * Function SavePcbCopy
     * writes the board data structures to \a a aFileName
     * but unlike SavePcbFile, does not make anything else
     * (no backup, borad fliename change, no flag changes ...)
     * Used under a project mgr to save under a new name the current board
     *
     * When not under a project mgr, the full SavePcbFile is used.
     * @param aFileName The file name to write.
     * @param aCreateProject will create an empty project alongside the board file
     * @return True if file was saved successfully.
     */
    bool SavePcbCopy( const wxString& aFileName, bool aCreateProject = false );

    // BOARD handling

    /**
     * Function Clear_Pcb
     * delete all and reinitialize the current board
     * @param aQuery = true to prompt user for confirmation, false to initialize silently
     * @param aFinal = if true, we are clearing the board to exit, so don't run more events
     */
    bool Clear_Pcb( bool aQuery, bool aFinal = false );

    ///> @copydoc PCB_BASE_FRAME::SetBoard()
    void SetBoard( BOARD* aBoard ) override;

    ///> @copydoc PCB_BASE_FRAME::GetModel()
    BOARD_ITEM_CONTAINER* GetModel() const override;

    ///> @copydoc PCB_BASE_FRAME::SetPageSettings()
    void SetPageSettings( const PAGE_INFO& aPageSettings ) override;

    /**
     * Function RecreateBOMFileFromBoard
     * Recreates a .cmp file from the current loaded board
     * this is the same as created by CvPcb.
     * can be used if this file is lost
     */
    void RecreateCmpFileFromBoard( wxCommandEvent& aEvent );

    /**
     * Function HarvestFootprintsToLibrary
     * Save footprints in a library:
     * @param aStoreInNewLib:
     *              true : save footprints in a existing lib. Existing footprints will be kept
     *              or updated.
     *              This lib should be in fp lib table, and is type is .pretty
     *              false: save footprints in a new lib. It it is an existing lib,
     *              previous footprints will be removed
     *
     * @param aLibName:
     *              optional library name to create, stops dialog call.
     *              must be called with aStoreInNewLib as true
     */
    void HarvestFootprintsToLibrary( bool aStoreInNewLib, const wxString& aLibName = wxEmptyString,
                                     wxString* aLibPath = NULL );

    /**
     * Function RecreateBOMFileFromBoard
     * Creates a BOM file from the current loaded board
     */
    void RecreateBOMFileFromBoard( wxCommandEvent& aEvent );

    /**
     * Function ExportToGenCAD
     * creates a file in  GenCAD 1.4 format from the current board.
     */
    void ExportToGenCAD( wxCommandEvent& event );

    /**
     * Function OnExportVRML
     * will export the current BOARD to a VRML file.
     */
    void OnExportVRML( wxCommandEvent& event );

    /**
     * Function ExportVRML_File
     * Creates the file(s) exporting current BOARD to a VRML file.
     *
     * @note When copying 3D shapes files, the new filename is build from the full path
     *       name, changing the separators by underscore.  This is needed because files
     *       with the same shortname can exist in different directories
     * @note ExportVRML_File generates coordinates in board units (BIU) inside the file.
     * @todo Use mm inside the file.  A general scale transform is applied to the whole
     *       file (1.0 to have the actual WRML unit im mm, 0.001 to have the actual WRML
     *       unit in meters.
     * @note For 3D models built by a 3D modeler, the unit is 0,1 inches.  A specific scale
     *       is applied to 3D models to convert them to internal units.
     *
     * @param aFullFileName = the full filename of the file to create
     * @param aMMtoWRMLunit = the VRML scaling factor:
     *      1.0 to export in mm. 0.001 for meters
     * @param aExport3DFiles = true to copy 3D shapes in the subir a3D_Subdir
     * @param aUseRelativePaths set to true to use relative paths instead of absolute paths
     *                          in the board VRML file URLs.
     * @param aUsePlainPCB set to true to export a board with no copper or silkskreen;
     *                          this is useful for generating a VRML file which can be
     *                          converted to a STEP model.
     * @param a3D_Subdir = sub directory where 3D shapes files are copied.  This is only used
     *                     when aExport3DFiles == true
     * @param aXRef = X value of PCB (0,0) reference point
     * @param aYRef = Y value of PCB (0,0) reference point
     * @return true if Ok.
     */
    bool ExportVRML_File( const wxString & aFullFileName, double aMMtoWRMLunit,
                          bool aExport3DFiles, bool aUseRelativePaths, bool aUsePlainPCB,
                          const wxString & a3D_Subdir, double aXRef, double aYRef );

    /**
     * Function OnExportIDF3
     * will export the current BOARD to a IDFv3 board and lib files.
     */
    void OnExportIDF3( wxCommandEvent& event );

    /**
     * Function OnExportHyperlynx
     * will export the current BOARD to a Hyperlynx HYP file.
     */
    void OnExportHyperlynx( wxCommandEvent& event );

    /**
     * Function Export_IDF3
     * Creates an IDF3 compliant BOARD (*.emn) and LIBRARY (*.emp) file.
     *
     * @param aPcb = a pointer to the board to be exported to IDF
     * @param aFullFileName = the full filename of the export file
     * @param aUseThou = set to true if the desired IDF unit is thou (mil)
     * @param aXRef = the board Reference Point in mm, X value
     * @param aYRef = the board Reference Point in mm, Y value
     * @return true if OK
     */
    bool Export_IDF3( BOARD* aPcb, const wxString& aFullFileName,
                      bool aUseThou, double aXRef, double aYRef );

    /**
     * Function OnExportSTEP
     * Exports the current BOARD to a STEP assembly.
     */
    void OnExportSTEP( wxCommandEvent& event );

    /**
     * Function ExportSpecctraFile
     * will export the current BOARD to a specctra dsn file.
     * See http://www.autotraxeda.com/docs/SPECCTRA/SPECCTRA.pdf for the
     * specification.
     * @return true if OK
     */
    bool ExportSpecctraFile( const wxString& aFullFilename );

    /**
     * Function ImportSpecctraSession
     * will import a specctra *.ses file and use it to relocate MODULEs and
     * to replace all vias and tracks in an existing and loaded BOARD.
     * See http://www.autotraxeda.com/docs/SPECCTRA/SPECCTRA.pdf for the
     * specification.
     */
    bool ImportSpecctraSession( const wxString& aFullFilename );

    // Footprint editing (see also PCB_BASE_FRAME)
    void ShowFootprintPropertiesDialog( MODULE* aFootprint );

    int ShowExchangeFootprintsDialog( MODULE* aModule, bool updateMode, bool selectedMode );

    /**
     * Function Exchange_Module
     * Replaces OldModule by NewModule, using OldModule settings:
     * position, orientation, pad netnames ...)
     * OldModule is deleted or put in undo list.
     * @param aExisting = footprint to replace
     * @param aNew = footprint to put
     * @param aCommit = commit that should store the changes
     */
    void ExchangeFootprint( MODULE* aExisting, MODULE* aNew, BOARD_COMMIT& aCommit,
                            bool deleteExtraTexts = true, bool resetTextLayers = true,
                            bool resetTextEffects = true, bool resetFabricationAttrs = true,
                            bool reset3DModels = true );

    // loading footprints: see PCB_BASE_FRAME

    /**
     * Function OnEditItemRequest
     * Install the corresponding dialog editor for the given item
     * @param aDC = the current device context
     * @param aItem = a pointer to the BOARD_ITEM to edit
     */
    void OnEditItemRequest( BOARD_ITEM* aItem ) override;

    void SwitchLayer( wxDC* DC, PCB_LAYER_ID layer ) override;

    /**
     * Function SetTrackSegmentWidth
     *  Modify one track segment width or one via diameter (using DRC control).
     *  Basic routine used by other routines when editing tracks or vias.
     *  Note that casting this to boolean will allow you to determine whether any action
     *  happened.
     * @param aTrackItem = the track segment or via to modify
     * @param aItemsListPicker = the list picker to use for an undo command
     *                           (can be NULL)
     * @param aUseNetclassValue = true to use NetClass value, false to use
     *                            current designSettings value
     */
    void SetTrackSegmentWidth( TRACK*             aTrackItem,
                               PICKED_ITEMS_LIST* aItemsListPicker,
                               bool               aUseNetclassValue );


    /**
     * Function Edit_Zone_Params
     * Edit params (layer, clearance, ...) for a zone outline
     */
    void Edit_Zone_Params( ZONE_CONTAINER* zone_container );

    // Properties dialogs
    void ShowTargetOptionsDialog( PCB_TARGET* aTarget );
    void ShowDimensionPropertiesDialog( DIMENSION* aDimension );
    void ShowGroupPropertiesDialog( PCB_GROUP* aGroup );
    void InstallNetlistFrame();

    /**
     * Function FetchNetlistFromSchematic
     * @param aNetlist a NETLIST owned by the caller.  This function fills it in.
     * @return true if a netlist was fetched.
     */
    enum FETCH_NETLIST_MODE { NO_ANNOTATION, QUIET_ANNOTATION, ANNOTATION_DIALOG };
    bool FetchNetlistFromSchematic( NETLIST& aNetlist, FETCH_NETLIST_MODE aMode );

    /**
     * Sends a command to Eeschema to re-annotate the schematic
     * @param aNetlist a NETLIST filled in by the caller.
     *        aMessage is the error message from eeSchem
     *        if aCommit is false it just test, if true it updates the schematic
     * @return false if failed due to standalone mode, true if a reply.
     */
    bool ReannotateSchematic( std::string& aNetlist );

    /**
     * Test if standalone mode.
     * @return true if in standalone, opens eeSchema, and opens the schematic for this project
     */
    bool TestStandalone( void );

    /**
     * Function DoUpdatePCBFromNetlist
     * An automated version of UpdatePCBFromNetlist which skips the UI dialog.
     * @param aNetlist
     * @param aUseTimestamps
     */
    void DoUpdatePCBFromNetlist( NETLIST& aNetlist, bool aUseTimestamps );

    /**
     * Reads a netlist from a file into a NETLIST object.
     *
     * @param aFilename is the netlist to load
     * @param aNetlist is the object to populate with data
     * @param aReporter is a #REPORTER object to display messages
     * @return true if the netlist was read successfully
     */
    bool ReadNetlistFromFile( const wxString &aFilename, NETLIST& aNetlist, REPORTER& aReporter );

    /**
     * Called after netlist is updated
     * @param aUpdater is the updater object that was run
     * @param aRunDragCommand is set to true if the drag command was invoked by this call
     */
    void OnNetlistChanged( BOARD_NETLIST_UPDATER& aUpdater, bool* aRunDragCommand );


#if defined( KICAD_SCRIPTING_WXPYTHON )
    /**
     * Function ScriptingConsoleEnableDisable
     * enables or disabled the scripting console
     */
    void ScriptingConsoleEnableDisable();
#endif

    void LockModule( MODULE* aModule, bool aLocked );

    /**
     * Function SendMessageToEESCHEMA
     * sends a message to the schematic editor so that it may move its cursor
     * to a part with the same reference as the objectToSync
     * @param objectToSync The object whose reference is used to synchronize Eeschema.
     */
    void SendMessageToEESCHEMA( BOARD_ITEM* objectToSync );

    /**
     * Sends a net name to eeschema for highlighting
     *
     * @param aNetName is the name of a net, or empty string to clear highlight
     */
    void SendCrossProbeNetName( const wxString& aNetName );

    void ShowChangedLanguage() override;

    /**
     * Function UpdateTitle
     * sets the main window title bar text.
     * <p>
     * If file name defined by PCB_SCREEN::m_FileName is not set, the title is set to the
     * application name appended with no file.  Otherwise, the title is set to the full path
     * and file name and read only is appended to the title if the user does not have write
     * access to the file.
     * </p>
     */
    void UpdateTitle();

    void On3DShapeLibWizard( wxCommandEvent& event );

    /**
     * Allows Pcbnew to install its preferences panel into the preferences dialog.
     */
    void InstallPreferences( PAGED_DIALOG* aParent, PANEL_HOTKEYS_EDITOR* aHotkeysPanel ) override;

    /**
     * Called after the preferences dialog is run.
     */
    void CommonSettingsChanged( bool aEnvVarsChanged, bool aTextVarsChanged ) override;

    void ProjectChanged() override;

    wxString GetCurrentFileName() const override;

    SELECTION& GetCurrentSelection() override;

    DECLARE_EVENT_TABLE()
};

#endif  // WXPCB_STRUCT_H_
