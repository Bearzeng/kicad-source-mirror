///////////////////////////////////////////////////////////////////////////
// C++ code generated with wxFormBuilder (version Oct 26 2018)
// http://www.wxformbuilder.org/
//
// PLEASE DO *NOT* EDIT THIS FILE!
///////////////////////////////////////////////////////////////////////////

#include "dialog_select_net_from_list_base.h"

///////////////////////////////////////////////////////////////////////////

DIALOG_SELECT_NET_FROM_LIST_BASE::DIALOG_SELECT_NET_FROM_LIST_BASE( wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style ) : DIALOG_SHIM( parent, id, title, pos, size, style )
{
	this->SetSizeHints( wxSize( -1,-1 ), wxDefaultSize );

	wxBoxSizer* bSizerMain;
	bSizerMain = new wxBoxSizer( wxVERTICAL );

	wxBoxSizer* bTopSizer;
	bTopSizer = new wxBoxSizer( wxHORIZONTAL );

	m_staticTextFilter = new wxStaticText( this, wxID_ANY, _("Net name filter:"), wxDefaultPosition, wxDefaultSize, 0 );
	m_staticTextFilter->Wrap( -1 );
	bTopSizer->Add( m_staticTextFilter, 0, wxALL|wxALIGN_CENTER_VERTICAL, 5 );

	m_textCtrlFilter = new wxTextCtrl( this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0 );
	bTopSizer->Add( m_textCtrlFilter, 1, wxBOTTOM|wxEXPAND|wxRIGHT|wxTOP, 5 );


	bTopSizer->Add( 0, 0, 0, wxEXPAND|wxRIGHT|wxLEFT, 10 );

	m_cbShowZeroPad = new wxCheckBox( this, wxID_ANY, _("Show zero pad nets"), wxDefaultPosition, wxDefaultSize, 0 );
	m_cbShowZeroPad->SetValue(true);
	bTopSizer->Add( m_cbShowZeroPad, 0, wxALL|wxALIGN_CENTER_VERTICAL, 5 );


	bSizerMain->Add( bTopSizer, 0, wxEXPAND|wxTOP|wxRIGHT|wxLEFT, 5 );

	m_netsList = new wxDataViewListCtrl( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxDV_HORIZ_RULES|wxDV_VERT_RULES );
	m_netsList->SetMinSize( wxSize( 500,300 ) );

	bSizerMain->Add( m_netsList, 1, wxEXPAND|wxRIGHT|wxLEFT, 10 );

	wxBoxSizer* bSizerListButtons;
	bSizerListButtons = new wxBoxSizer( wxHORIZONTAL );

	m_addNet = new wxBitmapButton( this, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, wxBU_AUTODRAW|0 );
	bSizerListButtons->Add( m_addNet, 0, wxTOP|wxBOTTOM|wxLEFT, 5 );

	m_renameNet = new wxBitmapButton( this, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, wxBU_AUTODRAW|0 );
	bSizerListButtons->Add( m_renameNet, 0, wxTOP|wxBOTTOM|wxLEFT, 5 );


	bSizerListButtons->Add( 20, 0, 0, wxEXPAND, 5 );

	m_deleteNet = new wxBitmapButton( this, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, wxBU_AUTODRAW|0 );
	bSizerListButtons->Add( m_deleteNet, 0, wxALL, 5 );


	bSizerListButtons->Add( 0, 0, 1, wxEXPAND, 5 );

	m_ReportButt = new wxButton( this, wxID_ANY, _("Create Report..."), wxDefaultPosition, wxDefaultSize, 0 );
	bSizerListButtons->Add( m_ReportButt, 0, wxALL|wxALIGN_CENTER_VERTICAL, 5 );


	bSizerMain->Add( bSizerListButtons, 0, wxEXPAND|wxALL, 5 );

	m_staticline1 = new wxStaticLine( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_HORIZONTAL );
	bSizerMain->Add( m_staticline1, 0, wxEXPAND | wxALL, 5 );

	wxBoxSizer* bSizerBottom;
	bSizerBottom = new wxBoxSizer( wxHORIZONTAL );

	m_sdbSizer = new wxStdDialogButtonSizer();
	m_sdbSizerOK = new wxButton( this, wxID_OK );
	m_sdbSizer->AddButton( m_sdbSizerOK );
	m_sdbSizerCancel = new wxButton( this, wxID_CANCEL );
	m_sdbSizer->AddButton( m_sdbSizerCancel );
	m_sdbSizer->Realize();

	bSizerBottom->Add( m_sdbSizer, 1, wxALL|wxEXPAND, 5 );


	bSizerMain->Add( bSizerBottom, 0, wxEXPAND|wxLEFT, 5 );


	this->SetSizer( bSizerMain );
	this->Layout();
	bSizerMain->Fit( this );

	this->Centre( wxBOTH );

	// Connect Events
	m_textCtrlFilter->Connect( wxEVT_COMMAND_TEXT_UPDATED, wxCommandEventHandler( DIALOG_SELECT_NET_FROM_LIST_BASE::onFilterChange ), NULL, this );
	m_cbShowZeroPad->Connect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( DIALOG_SELECT_NET_FROM_LIST_BASE::onFilterChange ), NULL, this );
	m_netsList->Connect( wxEVT_COMMAND_DATAVIEW_SELECTION_CHANGED, wxDataViewEventHandler( DIALOG_SELECT_NET_FROM_LIST_BASE::onSelChanged ), NULL, this );
	m_netsList->Connect( wxEVT_SIZE, wxSizeEventHandler( DIALOG_SELECT_NET_FROM_LIST_BASE::onListSize ), NULL, this );
	m_addNet->Connect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( DIALOG_SELECT_NET_FROM_LIST_BASE::onAddNet ), NULL, this );
	m_renameNet->Connect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( DIALOG_SELECT_NET_FROM_LIST_BASE::onRenameNet ), NULL, this );
	m_deleteNet->Connect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( DIALOG_SELECT_NET_FROM_LIST_BASE::onDeleteNet ), NULL, this );
	m_ReportButt->Connect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( DIALOG_SELECT_NET_FROM_LIST_BASE::onReport ), NULL, this );
}

DIALOG_SELECT_NET_FROM_LIST_BASE::~DIALOG_SELECT_NET_FROM_LIST_BASE()
{
	// Disconnect Events
	m_textCtrlFilter->Disconnect( wxEVT_COMMAND_TEXT_UPDATED, wxCommandEventHandler( DIALOG_SELECT_NET_FROM_LIST_BASE::onFilterChange ), NULL, this );
	m_cbShowZeroPad->Disconnect( wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler( DIALOG_SELECT_NET_FROM_LIST_BASE::onFilterChange ), NULL, this );
	m_netsList->Disconnect( wxEVT_COMMAND_DATAVIEW_SELECTION_CHANGED, wxDataViewEventHandler( DIALOG_SELECT_NET_FROM_LIST_BASE::onSelChanged ), NULL, this );
	m_netsList->Disconnect( wxEVT_SIZE, wxSizeEventHandler( DIALOG_SELECT_NET_FROM_LIST_BASE::onListSize ), NULL, this );
	m_addNet->Disconnect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( DIALOG_SELECT_NET_FROM_LIST_BASE::onAddNet ), NULL, this );
	m_renameNet->Disconnect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( DIALOG_SELECT_NET_FROM_LIST_BASE::onRenameNet ), NULL, this );
	m_deleteNet->Disconnect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( DIALOG_SELECT_NET_FROM_LIST_BASE::onDeleteNet ), NULL, this );
	m_ReportButt->Disconnect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( DIALOG_SELECT_NET_FROM_LIST_BASE::onReport ), NULL, this );

}
