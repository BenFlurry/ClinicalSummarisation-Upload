#include "pch.h"
#include "MainWindow.xaml.h"
#include "Helpers.h"

#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.Web.WebView2.Core.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.h>

// --- Required headers for Virtual Host Mapping ---
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Storage.h>

#include <winrt/Microsoft.UI.Dispatching.h>
#include <sstream>
#include <filesystem>
#include <chrono>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;

namespace winrt::ClinicalSummarisation::implementation {

    void MainWindow::viewGuidance_PointerEntered(IInspectable const&, winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&) {
        try {
            auto dataPath = Helpers::GetGuidelinesDataPath();
            auto indexedDocs = GuidelineIndexer::GetIndexedGuidelines(dataPath);

            if (indexedDocs.empty()) {
                ToolTipService::SetToolTip(viewGuidance_btn(), winrt::box_value(L"Add PDF documents in Settings"));
            }
            else {
                ToolTipService::SetToolTip(viewGuidance_btn(), winrt::box_value(L"View NICE Guidance"));
            }
        }
        catch (...) {
            ToolTipService::SetToolTip(viewGuidance_btn(), winrt::box_value(L"Add PDF documents in Settings"));
        }
    }

    void MainWindow::viewGuidance_Click(IInspectable const&, RoutedEventArgs const&) {
        RecordingView().Visibility(Visibility::Collapsed);
        AppraisalsView().Visibility(Visibility::Collapsed);
        GuidanceView().Visibility(Visibility::Visible);

        GuidanceStatusText().Text(L"Searching NICE guidelines...");
        GuidanceSpinner().Visibility(Visibility::Visible);
        GuidanceResultsPanel().Children().Clear();
        PdfTabsPanel().Children().Clear();

        m_selectedGuidanceIdx = -1;
        m_webViewMapped = false;
        m_currentlyLoadedPdf = "";

        std::string summary = m_summarisation;

        std::thread([this, summary]() {
            if (!m_isRagReady) {
                if (m_ragLoadFuture.valid()) m_ragLoadFuture.wait();
            }

            if (!m_isRagReady || !m_rag) {
                this->DispatcherQueue().TryEnqueue([this]() {
                    GuidanceStatusText().Text(L"Guidance unavailable - RAG failed to load");
                    GuidanceSpinner().Visibility(Visibility::Collapsed);
                    });
                return;
            }

            // try and reload if not loaded (could be from adding docs)
            if (!m_rag->HasIndex()) {
                try { m_rag->ReloadIndex(); }
                catch (...) {}
            }

            // definitely not loaded -> error
            if (!m_rag->HasIndex()) {
                this->DispatcherQueue().TryEnqueue([this]() {
                    GuidanceStatusText().Text(L"No guidelines indexed yet. Add documents via Settings.");
                    GuidanceSpinner().Visibility(Visibility::Collapsed);
                    });
                return;
            }

            std::vector<GuidelineResult> results;
            // get top 5
            try { results = m_rag->Query(summary, 5); }
            catch (const std::exception& ex) {
                std::string errMsg = ex.what();
                OutputDebugStringA(("RAG Query exception: " + errMsg + "\n").c_str());
                this->DispatcherQueue().TryEnqueue([this, errMsg]() {
                    GuidanceStatusText().Text(winrt::to_hstring("Failed to query guidelines: " + errMsg));
                    GuidanceSpinner().Visibility(Visibility::Collapsed);
                    });
                return;
            }
            catch (...) {
                OutputDebugStringA("RAG Query: unknown exception\n");
                this->DispatcherQueue().TryEnqueue([this]() {
                    GuidanceStatusText().Text(L"Failed to query guidelines (unknown error)");
                    GuidanceSpinner().Visibility(Visibility::Collapsed);
                    });
                return;
            }

            m_ragResults = results;

            // capture diagnostic info on the background thread for the UI
            std::string diagInfo;
            if (m_rag && m_rag->HasIndex()) {
                auto dataPath = Helpers::GetGuidelinesDataPath();
                auto indexedDocs = GuidelineIndexer::GetIndexedGuidelines(dataPath);
                diagInfo = "HNSW has elements, " + std::to_string(indexedDocs.size()) +
                    " guideline(s) in DB, query returned " + std::to_string(results.size()) + " results.";
            }

            this->DispatcherQueue().TryEnqueue([this, results, diagInfo]() {
                GuidanceSpinner().Visibility(Visibility::Collapsed);
                GuidanceStatusText().Text(L"");
                GuidanceResultsPanel().Children().Clear();

                if (results.empty()) {
                    TextBlock noResults;
                    noResults.Text(winrt::to_hstring("No matching guidelines found. " + diagInfo));
                    noResults.TextWrapping(TextWrapping::Wrap);
                    noResults.Foreground(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(
                        winrt::Windows::UI::ColorHelper::FromArgb(255, 100, 116, 139)));
                    GuidanceResultsPanel().Children().Append(noResults);
                    return;
                }

                // generate tab for each pdf
                std::vector<std::string> uniquePdfs;
                for (const auto& r : results) {
                    if (std::find(uniquePdfs.begin(), uniquePdfs.end(), r.guidelineName) == uniquePdfs.end()) {
                        uniquePdfs.push_back(r.guidelineName);
                    }
                }

                for (const auto& pdfName : uniquePdfs) {
                    Button tabBtn;

                    // truncate name
                    std::string shortName = TruncateFilename(pdfName, 15); 
                    tabBtn.Content(winrt::box_value(winrt::to_hstring(shortName)));
                    tabBtn.Tag(winrt::box_value(winrt::to_hstring(pdfName)));

                    // add tooltip on hover for full name display
                    ToolTipService::SetToolTip(tabBtn, winrt::box_value(winrt::to_hstring(pdfName)));

                    tabBtn.CornerRadius({ 8, 8, 8, 8 });
                    tabBtn.FontWeight(winrt::Windows::UI::Text::FontWeight{ 600 });

                    std::string targetPdf = pdfName;
                    tabBtn.Click([this, targetPdf](IInspectable const&, RoutedEventArgs const&) {
                        int firstPage = 1;
                        for (const auto& r : m_ragResults) {
                            if (r.guidelineName == targetPdf) { firstPage = r.pageNumber; break; }
                        }
                        // open the pdf on the first page when loading
                        SwitchToPdf(targetPdf, firstPage);
                        });
                    // add pdf to left panel
                    PdfTabsPanel().Children().Append(tabBtn);
                }


                // generate cards
                TextBlock header;
                header.Text(L"Relevant Guidance");
                header.FontWeight(winrt::Windows::UI::Text::FontWeight{ 600 });
                header.FontSize(16);
                header.Margin({ 0, 0, 0, 8 });
                GuidanceResultsPanel().Children().Append(header);

                for (size_t i = 0; i < results.size(); ++i) {
                    const auto& r = results[i];

                    Button card;
                    card.HorizontalAlignment(HorizontalAlignment::Stretch);
                    card.HorizontalContentAlignment(HorizontalAlignment::Stretch);
                    card.CornerRadius({ 12, 12, 12, 12 });
                    card.Padding({ 12, 10, 12, 10 });
                    card.BorderThickness({ 1, 1, 1, 1 });

                    StackPanel sp;
                    sp.Spacing(4);

                    // truncated filename label
                    std::string shortName = TruncateFilename(r.guidelineName, 35);
                    TextBlock fileLabel;
                    fileLabel.Text(winrt::to_hstring(shortName));
                    fileLabel.FontWeight(winrt::Windows::UI::Text::FontWeight{ 600 });
                    fileLabel.FontSize(13);
                    fileLabel.Foreground(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(
                        winrt::Windows::UI::ColorHelper::FromArgb(255, 11, 59, 96))); // Dark Blue
                    sp.Children().Append(fileLabel);

                    // page number label
                    TextBlock pageLabel;
                    pageLabel.Text(winrt::hstring(L"Page " + winrt::to_hstring(r.pageNumber)));
                    pageLabel.FontWeight(winrt::Windows::UI::Text::FontWeight{ 600 });
                    pageLabel.FontSize(12);
                    pageLabel.Foreground(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(
                        winrt::Windows::UI::ColorHelper::FromArgb(255, 71, 85, 105))); // Slate Gray
                    sp.Children().Append(pageLabel);

                    // rag snippet preview
                    TextBlock textPreview;
                    std::string preview = r.chunkText.substr(0, 150);
                    if (r.chunkText.size() > 150) preview += "...";
                    textPreview.Text(winrt::to_hstring(preview));
                    textPreview.TextWrapping(TextWrapping::Wrap);
                    textPreview.FontSize(12);
                    textPreview.Foreground(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(
                        winrt::Windows::UI::ColorHelper::FromArgb(255, 71, 85, 105))); // Slate Gray
                    sp.Children().Append(textPreview);

                    card.Content(sp);
                    ToolTipService::SetToolTip(card, winrt::box_value(winrt::to_hstring(r.guidelineName)));

                    size_t idx = i;
                    std::string targetPdf = r.guidelineName;
                    int targetPage = r.pageNumber;

                    // update if clicked
                    card.Click([this, idx, targetPage, targetPdf](IInspectable const&, RoutedEventArgs const&) {
                        if (idx < m_ragResults.size()) {
                            m_selectedGuidanceIdx = static_cast<int>(idx);
                            UpdateGuidanceCardSelection();
                            SwitchToPdf(targetPdf, targetPage);
                        }
                        });

                    GuidanceResultsPanel().Children().Append(card);
                }

                // initialise first result
                m_selectedGuidanceIdx = 0;
                UpdateGuidanceCardSelection();
                SwitchToPdf(results[0].guidelineName, results[0].pageNumber);

                });
            }).detach();
    }


    void MainWindow::UpdateGuidanceCardSelection() {
        auto children = GuidanceResultsPanel().Children();
        for (uint32_t i = 1; i < children.Size(); ++i) {
            auto card = children.GetAt(i).try_as<Button>();
            if (!card) continue;

            bool selected = (static_cast<int>(i) - 1 == m_selectedGuidanceIdx);
            card.Background(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(
                selected ? winrt::Windows::UI::ColorHelper::FromArgb(50, 79, 172, 254)
                : winrt::Windows::UI::ColorHelper::FromArgb(20, 0, 0, 0)));
        }
    }

    void MainWindow::closeGuidance_Click(IInspectable const&, RoutedEventArgs const&) {
        m_webViewMapped = false;
        GuidanceView().Visibility(Visibility::Collapsed);
        RecordingView().Visibility(Visibility::Visible);
    }

    winrt::fire_and_forget MainWindow::LoadPdfWithAllHighlights(std::vector<GuidelineResult> results, int targetPage) {
        if (results.empty()) co_return;
        auto dispatcher = this->DispatcherQueue();

        
        co_await PdfWebView().EnsureCoreWebView2Async();
        auto core = PdfWebView().CoreWebView2();
        if (!core) co_return;

        auto packagePath = winrt::Windows::ApplicationModel::Package::Current().InstalledLocation().Path();
        std::filesystem::path viewerAssetsPath = std::filesystem::path(packagePath.c_str()) / L"pdfjs";

        core.SetVirtualHostNameToFolderMapping(
            L"viewer.local", viewerAssetsPath.wstring().c_str(),
            winrt::Microsoft::Web::WebView2::Core::CoreWebView2HostResourceAccessKind::Allow
        );

        std::filesystem::path pdfDirectoryPath = Helpers::GetGuidelinesDataPath();
        core.SetVirtualHostNameToFolderMapping(
            L"guidelines.local", pdfDirectoryPath.wstring().c_str(),
            winrt::Microsoft::Web::WebView2::Core::CoreWebView2HostResourceAccessKind::Allow
        );

        std::string jsonStr = "[";

        // bounding box
        for (size_t i = 0; i < results.size(); ++i) {
            jsonStr += "{\"p\":" + std::to_string(results[i].pageNumber) +
                ",\"x0\":" + std::to_string(results[i].bboxX0) +
                ",\"y0\":" + std::to_string(results[i].bboxY0) +
                ",\"x1\":" + std::to_string(results[i].bboxX1) +
                ",\"y1\":" + std::to_string(results[i].bboxY1) + "}";
            if (i < results.size() - 1) jsonStr += ",";
        }
        jsonStr += "]";

        winrt::hstring hName = winrt::to_hstring(results[0].guidelineName);
        // local opening of pdf
        std::wstring pdfFileUrl = L"https://guidelines.local/" + std::wstring(hName.c_str());
        winrt::hstring encodedPdfUrl = winrt::Windows::Foundation::Uri::EscapeComponent(pdfFileUrl);

        winrt::hstring hJson = winrt::to_hstring(jsonStr);
        winrt::hstring encodedJson = winrt::Windows::Foundation::Uri::EscapeComponent(hJson);

        std::wstringstream uriStream;

        // open at page with highlights
        uriStream << L"https://viewer.local/web/viewer.html"
            << L"?file=" << encodedPdfUrl.c_str()
            << L"&rag_highlights=" << encodedJson.c_str()
            << L"#page=" << targetPage; 

        core.Navigate(winrt::hstring(uriStream.str()));
        GuidanceResultsPanel().Focus(FocusState::Programmatic);
    }

    void MainWindow::UpdateTabSelection() {
        auto tabs = PdfTabsPanel().Children();
        for (uint32_t i = 0; i < tabs.Size(); ++i) {
            auto btn = tabs.GetAt(i).try_as<Button>();
            if (!btn) continue;

            std::string tabPdfName = winrt::to_string(winrt::unbox_value<winrt::hstring>(btn.Tag()));
            bool isSelected = (tabPdfName == m_currentlyLoadedPdf);

            btn.Background(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(
                isSelected ? winrt::Windows::UI::ColorHelper::FromArgb(255, 11, 59, 96)
                : winrt::Windows::UI::ColorHelper::FromArgb(50, 255, 255, 255)));

            btn.Foreground(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(
                isSelected ? winrt::Windows::UI::Colors::White()
                : winrt::Windows::UI::ColorHelper::FromArgb(255, 11, 59, 96)));
        }
    }

    void MainWindow::SwitchToPdf(const std::string& pdfName, int targetPage) {
        if (pdfName == m_currentlyLoadedPdf) {
            std::wstring script = L"PDFViewerApplication.page = " + std::to_wstring(targetPage) + L";";
            PdfWebView().CoreWebView2().ExecuteScriptAsync(script);
            return;
        }

        m_currentlyLoadedPdf = pdfName;
        UpdateTabSelection();

        // filter highlights for only the newly selected PDF
        std::vector<GuidelineResult> pdfSpecificResults;
        for (const auto& res : m_ragResults) {
            if (res.guidelineName == pdfName) {
                pdfSpecificResults.push_back(res);
            }
        }

        LoadPdfWithAllHighlights(pdfSpecificResults, targetPage);
    }

    std::string MainWindow::TruncateFilename(const std::string& name, size_t maxLength) {
        if (name.length() > maxLength) {
            return name.substr(0, maxLength) + "...";
        }
        return name;
    }
}