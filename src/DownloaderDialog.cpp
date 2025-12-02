#include "DownloaderDialog.h"
#include "DownloadModel.h"
#include <cinder/app/App.h>
#include <cinder/CinderImGui.h>

// C:\Users\chidi\Documents\code\default_venv\Scripts

namespace downloader
{

bool drawDownloaderDialog( const ci::ivec2 &size, IDownloadModel &model )
{
    bool isOpen = true;
    const int fontSize = ImGui::GetFontSize(); // * ImGui::GetIO().FontGlobalScale;
    ImGui::SetNextWindowSize( ImVec2( fontSize * size.x, fontSize * size.y ), ImGuiCond_Always );

    auto constexpr const *title = "Load Images";

    ImGui::OpenPopup( title, ImGuiPopupFlags_None );
    if( ImGui::BeginPopupModal( title, nullptr, ImGuiWindowFlags_None ) )
    {
        // TODO disable if mode.isDownloading()
        ImGui::Text( "Downloader" );
        ImGui::Text( "%s",  model.getDownloaderPath().c_str() );
        if( ImGui::Button( "Browse..." ) )
        {
            auto const path = ci::app::getOpenFilePath( "", { "exe" } );
            if( !path.string().empty() )
            {
                model.setDownloaderPath( path.string() );
            }
        }

        ImGui::Dummy( ImVec2( 100, ImGui::GetTextLineHeight() * 0.86f ) );
        ImGui::Separator();
        ImGui::Dummy( ImVec2( 100, ImGui::GetTextLineHeight() * 0.86f ) );

        //FilePath to yt-dlp
        ImGui::Text( "To Download" );
        int currIndex = -1;
        ImGui::SetNextItemWidth( ImGui::GetContentRegionAvailWidth() );
        if( ImGui::ListBox( "##To Download", &currIndex, model.getUrlList(), 10 ) )
        {

        }
        if( ImGui::Button( "Del" ) )
        {
            model.deleteUrl( currIndex );
        }
        ImGui::SameLine();
        if( ImGui::Button( "Add" ) )
        {

        }
        ImGui::Text( "Outputs" );
        ImGui::SetNextItemWidth( ImGui::GetContentRegionAvailWidth() );
        ImGui::ListBox( "##Outputs", &currIndex, model.getProcessOutputs(), 10 );
                                   //ImVec2( 0, 0 ), ImGuiInputTextFlags_ReadOnly );

        if( ImGui::Button( "Download" ) )
        {
            model.downloadAll();
        }
        ImGui::SameLine();
        // TODO end disable if mode.isDownloading()
        if( ImGui::Button( "Cancel Download" ) )
        {
            model.cancelDownload();
        }
        //List of files
        // +, - of files
        // download button.
        // cancel download.

        //ImGui::BeginChild( "Download Status" );

        //ImGui::EndChild();

        const ImVec2 btnSize( fontSize * 3, 0.0f );
        // ImGui::SetCursorPosY( std::max<float>( ImGui::GetContentRegionAvail().y - ImGui::GetTextLineHeight() -
        // ImGui::GetStyle().ItemSpacing.y, ImGui::GetCursorPosY() ) );
        ImGui::SetCursorPosX( ImGui::GetContentRegionAvail().x - ( btnSize.x ) );
        if( ImGui::Button( "Close", btnSize ) )
        {
            isOpen = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    return isOpen;
}

}// end namespace
