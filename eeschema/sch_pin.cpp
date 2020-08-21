/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2018 CERN
 * Copyright (C) 2019 KiCad Developers, see change_log.txt for contributors.
 * @author Jon Evans <jon@craftyjon.com>
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
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <lib_pin.h>
#include <sch_component.h>
#include <sch_pin.h>
#include <sch_sheet_path.h>
#include <sch_edit_frame.h>


SCH_PIN::SCH_PIN( LIB_PIN* aLibPin, SCH_COMPONENT* aParentComponent ) :
    SCH_ITEM( aParentComponent, SCH_PIN_T ),
    m_libPin( aLibPin )
{
    SetPosition( aLibPin->GetPosition() );
    m_isDangling = true;
}


SCH_PIN::SCH_PIN( const SCH_PIN& aPin ) :
        SCH_ITEM( aPin )
{
    m_alt = aPin.m_alt;
    m_libPin = aPin.m_libPin;
    m_position = aPin.m_position;
    m_isDangling = aPin.m_isDangling;
}


SCH_PIN& SCH_PIN::operator=( const SCH_PIN& aPin )
{
    SCH_ITEM::operator=( aPin );

    m_alt = aPin.m_alt;
    m_libPin = aPin.m_libPin;
    m_position = aPin.m_position;
    m_isDangling = aPin.m_isDangling;

    return *this;
}


wxString SCH_PIN::GetName() const
{
    if( !m_alt.IsEmpty() )
        return m_alt;

    return m_libPin->GetName();
}


ELECTRICAL_PINTYPE SCH_PIN::GetType() const
{
    if( !m_alt.IsEmpty() )
        m_libPin->GetAlt( m_alt ).m_Type;

    return m_libPin->GetType();
}


GRAPHIC_PINSHAPE SCH_PIN::GetShape() const
{
    if( !m_alt.IsEmpty() )
        m_libPin->GetAlt( m_alt ).m_Shape;

    return m_libPin->GetShape();
}


int SCH_PIN::GetOrientation() const
{
    return m_libPin->GetOrientation();
}


int SCH_PIN::GetLength() const
{
    return m_libPin->GetLength();
}


bool SCH_PIN::Matches( wxFindReplaceData& aSearchData, void* aAuxDat )
{
    if( !( aSearchData.GetFlags() & FR_SEARCH_ALL_PINS ) )
        return false;

    return EDA_ITEM::Matches( GetName(), aSearchData )
                || EDA_ITEM::Matches( GetNumber(), aSearchData );
}


bool SCH_PIN::Replace( wxFindReplaceData& aSearchData, void* aAuxData )
{
    bool isReplaced = false;

    /* TODO: waiting on a way to override pins in the schematic...
    isReplaced |= EDA_ITEM::Replace( aSearchData, m_name );
    isReplaced |= EDA_ITEM::Replace( aSearchData, m_number );
     */

    return isReplaced;
}


SCH_COMPONENT* SCH_PIN::GetParentComponent() const
{
    return static_cast<SCH_COMPONENT*>( GetParent() );
}


wxString SCH_PIN::GetSelectMenuText( EDA_UNITS aUnits ) const
{
    return wxString::Format( "%s %s",
                             GetParentComponent()->GetSelectMenuText( aUnits ),
                             m_libPin->GetSelectMenuText( aUnits ) );
}


void SCH_PIN::GetMsgPanelInfo( EDA_DRAW_FRAME* aFrame, MSG_PANEL_ITEMS& aList )
{
    wxString msg;

    aList.push_back( MSG_PANEL_ITEM( _( "Type" ), _( "Pin" ), CYAN ) );

    if( m_libPin->GetUnit() == 0 )
        msg = _( "All" );
    else
        msg.Printf( wxT( "%d" ), m_libPin->GetUnit() );

    aList.push_back( MSG_PANEL_ITEM( _( "Unit" ), msg, BROWN ) );

    if( m_libPin->GetConvert() == LIB_ITEM::LIB_CONVERT::BASE )
        msg = _( "no" );
    else if( m_libPin->GetConvert() == LIB_ITEM::LIB_CONVERT::DEMORGAN )
        msg = _( "yes" );
    else
        msg = wxT( "?" );

    aList.push_back( MSG_PANEL_ITEM( _( "Converted" ), msg, BROWN ) );

    aList.push_back( MSG_PANEL_ITEM( _( "Name" ), GetName(), DARKCYAN ) );
    aList.push_back( MSG_PANEL_ITEM( _( "Number" ), msg, DARKCYAN ) );
    aList.push_back( MSG_PANEL_ITEM( _( "Type" ), ElectricalPinTypeGetText( GetType() ), RED ) );

    msg = PinShapeGetText( GetShape() );
    aList.push_back( MSG_PANEL_ITEM( _( "Style" ), msg, BLUE ) );

    msg = IsVisible() ? _( "Yes" ) : _( "No" );
    aList.push_back( MSG_PANEL_ITEM( _( "Visible" ), msg, DARKGREEN ) );

    // Display pin length
    msg = StringFromValue( aFrame->GetUserUnits(), GetLength(), true );
    aList.push_back( MSG_PANEL_ITEM( _( "Length" ), msg, MAGENTA ) );

    msg = PinOrientationName( (unsigned) PinOrientationIndex( GetOrientation() ) );
    aList.push_back( MSG_PANEL_ITEM( _( "Orientation" ), msg, DARKMAGENTA ) );

    msg = MessageTextFromValue( aFrame->GetUserUnits(), m_position.x, true );
    aList.emplace_back( _( "Pos X" ), msg, DARKMAGENTA );

    msg = MessageTextFromValue( aFrame->GetUserUnits(), m_position.y, true );
    aList.emplace_back( _( "Pos Y" ), msg, DARKMAGENTA );

    aList.emplace_back( GetParentComponent()->GetField( REFERENCE )->GetShownText(),
                        GetParentComponent()->GetField( VALUE )->GetShownText(), DARKCYAN );

#if defined(DEBUG)

    SCH_EDIT_FRAME* frame = dynamic_cast<SCH_EDIT_FRAME*>( aFrame );

    if( !frame )
        return;

    SCH_CONNECTION* conn = Connection( frame->GetCurrentSheet() );

    if( conn )
        conn->AppendInfoToMsgPanel( aList );

#endif

}


void SCH_PIN::ClearDefaultNetName( const SCH_SHEET_PATH* aPath )
{
    std::lock_guard<std::recursive_mutex> lock( m_netmap_mutex );

    if( aPath )
        m_net_name_map.erase( *aPath );
    else
        m_net_name_map.clear();
}


wxString SCH_PIN::GetDefaultNetName( const SCH_SHEET_PATH aPath )
{
    if( m_libPin->IsPowerConnection() )
        return m_libPin->GetName();

    std::lock_guard<std::recursive_mutex> lock( m_netmap_mutex );

    if( m_net_name_map.count( aPath ) > 0 )
        return m_net_name_map.at( aPath );

    wxString name = "Net-(";

    name << GetParentComponent()->GetRef( &aPath );

    bool annotated = true;

    // Add timestamp for uninitialized components
    if( name.Last() == '?' )
    {
        name << GetParentComponent()->m_Uuid.AsString();
        annotated = false;
    }

    name << "-Pad" << m_libPin->GetNumber() << ")";

    if( annotated )
        m_net_name_map[ aPath ] = name;

    return name;
}


wxPoint SCH_PIN::GetTransformedPosition() const
{
    TRANSFORM t = GetParentComponent()->GetTransform();
    return ( t.TransformCoordinate( GetLocalPosition() ) + GetParentComponent()->GetPosition() );
}


const EDA_RECT SCH_PIN::GetBoundingBox() const
{
    TRANSFORM t = GetParentComponent()->GetTransform();
    EDA_RECT  r = m_libPin->GetBoundingBox();

    r.RevertYAxis();

    r = t.TransformCoordinate( r );
    r.Offset( GetParentComponent()->GetPosition() );

    return r;
}


bool SCH_PIN::HitTest( const wxPoint& aPosition, int aAccuracy ) const
{
    EDA_RECT rect = GetBoundingBox();
    return rect.Inflate( aAccuracy ).Contains( aPosition );
}


bool SCH_PIN::ConnectionPropagatesTo( const EDA_ITEM* aItem ) const
{
    // Reciprocal checking is done in CONNECTION_GRAPH anyway
    return !( m_libPin->GetType() == ELECTRICAL_PINTYPE::PT_NC );
}
