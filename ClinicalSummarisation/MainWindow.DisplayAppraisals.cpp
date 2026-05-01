#include "pch.h"
#include "MainWindow.xaml.h"

#include <winrt/Windows.UI.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <algorithm>
#include <set>
#include <string_view> 
#include <winrt/Microsoft.UI.Text.h>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Windows::Data::Json;

namespace winrt::ClinicalSummarisation::implementation {
    void MainWindow::appraisalHistory_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&) {
        // switch Views
        RecordingView().Visibility(Visibility::Collapsed);
        AppraisalsView().Visibility(Visibility::Visible);

        // reset search UI
        NameSearchBox().Text(L"");
        TagSearchBox().Text(L"");
        m_activeFilters.clear();

        // load Data
        LoadAppraisalsAsync();
    }

    // load data from JSON
    winrt::fire_and_forget MainWindow::LoadAppraisalsAsync() {
        m_allAppraisals.clear();
        try {
            auto localFolder = winrt::Windows::Storage::ApplicationData::Current().LocalFolder();
            auto item = co_await localFolder.TryGetItemAsync(L"appraisals.json");
            if (!item) co_return;

            auto file = item.as<winrt::Windows::Storage::StorageFile>();
            auto jsonString = co_await winrt::Windows::Storage::FileIO::ReadTextAsync(file);
            JsonObject root;

            if (JsonObject::TryParse(jsonString, root)) {
                for (auto const& pair : root) {
                    JsonObject val = pair.Value().GetObject();
                    AppraisalItem app;
                    app.Name = pair.Key();
                    app.Date = val.GetNamedString(L"date", L"");
                    app.Summary = val.GetNamedString(L"summary", L"");
                    app.Notes = val.GetNamedString(L"notes", L"");

                    JsonArray tagsArr = val.GetNamedArray(L"tags", JsonArray());
                    for (auto t : tagsArr) app.Tags.push_back(t.GetString().c_str());

                    m_allAppraisals.push_back(app);
                }
            }
        }
        catch (...) {}

        PopulateTagFilterList();

        // manual update
        RenderAppraisalsList();
    }

    // tag filter list
    void MainWindow::PopulateTagFilterList(std::wstring searchQuery) {
        TagFilterListView().Items().Clear();
        std::set<std::wstring> uniqueTags;
        for (auto const& app : m_allAppraisals) {
            for (auto const& t : app.Tags) uniqueTags.insert(t);
        }

        for (auto const& tag : uniqueTags) {
            if (!searchQuery.empty() && tag.find(searchQuery) == std::wstring::npos) continue;

            CheckBox cb;
            cb.Content(winrt::box_value(tag));
            cb.IsChecked(m_activeFilters.count(tag) > 0);

            cb.Checked([this, tag](auto&&...) { m_activeFilters.insert(tag); RenderAppraisalsList(); });
            cb.Unchecked([this, tag](auto&&...) { m_activeFilters.erase(tag); RenderAppraisalsList(); });

            TagFilterListView().Items().Append(cb);
        }
    }

    // renders appraisals grid
    void MainWindow::RenderAppraisalsList() {
        // clear grid
        AppraisalsListContainer().Items().Clear();

        std::wstring nameQuery = NameSearchBox().Text().c_str();

        // filter items
        std::vector<AppraisalItem> filtered;
        for (auto const& app : m_allAppraisals) {
            // filter name
            if (!nameQuery.empty() && std::wstring_view(app.Name).find(nameQuery) == std::wstring::npos) continue;

            // filter tags
            if (!m_activeFilters.empty()) {
                bool match = false;
                for (auto const& t : app.Tags) if (m_activeFilters.count(t)) { match = true; break; }
                if (!match) continue;
            }
            filtered.push_back(app);
        }

        // sort by newest date
        std::sort(filtered.begin(), filtered.end(), [](const AppraisalItem& a, const AppraisalItem& b) {
            return a.Date > b.Date;
            });

        // generate UI (easier to do programatically
        for (auto const& app : filtered) {

            // button wrapper
            Button itemButton;
            itemButton.Margin({ 6, 6, 6, 6 });
            itemButton.HorizontalContentAlignment(HorizontalAlignment::Stretch);
            itemButton.VerticalContentAlignment(VerticalAlignment::Stretch);
            itemButton.Background(Media::SolidColorBrush(winrt::Windows::UI::Colors::Transparent()));
            itemButton.BorderThickness({ 0,0,0,0 });
            itemButton.Padding({ 0,0,0,0 });

            // store ID for click handler
            itemButton.Tag(winrt::box_value(app.Name));
            itemButton.Click({ this, &MainWindow::OnHistoryItemClick });

            // glass border
            Border glassBorder;
            // fixed width, we create columns if space is avaliable
            glassBorder.Width(300); 
            glassBorder.Padding({ 16, 16, 16, 16 });
            glassBorder.CornerRadius({ 20, 20, 20, 20 });

            // semi-transparent white background and border
            glassBorder.Background(Media::SolidColorBrush(winrt::Windows::UI::ColorHelper::FromArgb(102, 255, 255, 255)));
            glassBorder.BorderBrush(Media::SolidColorBrush(winrt::Windows::UI::ColorHelper::FromArgb(179, 255, 255, 255)));
            glassBorder.BorderThickness({ 1, 1, 1, 1 });

            // container for text
            StackPanel card;
            card.Spacing(6);

            // title
            TextBlock title;
            title.Text(app.Name);
            title.FontWeight(winrt::Microsoft::UI::Text::FontWeights::Bold());
            title.FontSize(16);
            title.Foreground(Media::SolidColorBrush(winrt::Windows::UI::ColorHelper::FromArgb(255, 11, 59, 96))); 
            title.TextTrimming(TextTrimming::CharacterEllipsis);

            // date
            std::wstring_view dateView(app.Date);
            winrt::hstring dateStr = L"Date: ";
            winrt::hstring d = dateView.length() >= 10 ? winrt::hstring(dateView.substr(0, 10)) : app.Date;
            dateStr = dateStr + d;

            TextBlock date;
            date.Text(dateStr);
            date.FontSize(11);
            date.Foreground(Media::SolidColorBrush(winrt::Windows::UI::ColorHelper::FromArgb(255, 11, 59, 96))); 
            date.Opacity(0.7);

            // tags
            std::wstring tagsJoined = L"Tags: ";
            for (size_t i = 0; i < app.Tags.size(); ++i) {
                tagsJoined += app.Tags[i];
                if (i < app.Tags.size() - 1) tagsJoined += L", ";
            }

            TextBlock tags;
            tags.Text(tagsJoined);
            tags.TextWrapping(TextWrapping::NoWrap);
            tags.TextTrimming(TextTrimming::CharacterEllipsis);
            tags.FontSize(11);
            tags.FontStyle(winrt::Windows::UI::Text::FontStyle::Italic);
            tags.Foreground(Media::SolidColorBrush(winrt::Windows::UI::ColorHelper::FromArgb(255, 11, 59, 96))); 

            // summary
            TextBlock summary;
            summary.Text(app.Summary);
            summary.TextWrapping(TextWrapping::Wrap);
            summary.TextTrimming(TextTrimming::CharacterEllipsis);
            summary.MaxHeight(60); 
            summary.Margin({ 0, 4, 0, 0 });
            summary.Foreground(Media::SolidColorBrush(winrt::Windows::UI::ColorHelper::FromArgb(255, 30, 41, 59))); 

            // assemble the card
            card.Children().Append(title);
            card.Children().Append(date);
            card.Children().Append(tags);
            card.Children().Append(summary);

            glassBorder.Child(card);
            itemButton.Content(glassBorder);

            // append to the GridView
            AppraisalsListContainer().Items().Append(itemButton);
        }
    }

    void MainWindow::OnHistoryItemClick(IInspectable const& sender, RoutedEventArgs const&) {
        // recover the name from the tag
        auto button = sender.as<Button>();
        auto nameBoxed = button.Tag();
        if (!nameBoxed) return;

        // id of the found item
        winrt::hstring appraisalName = winrt::unbox_value<winrt::hstring>(nameBoxed);

        // find the appraisal
        AppraisalItem* foundItem = nullptr;
        for (auto& app : m_allAppraisals) {
            if (app.Name == appraisalName) {
                foundItem = &app;
                break;
            }
        }

        // populate dialogue fields
        if (foundItem) {
            HistoryNameBox().Text(foundItem->Name);
            HistorySummaryBox().Text(foundItem->Summary);
            HistoryNotesBox().Text(foundItem->Notes);

            // convert tags to a string
            std::wstring tagsJoined;
            for (size_t i = 0; i < foundItem->Tags.size(); ++i) {
                tagsJoined += foundItem->Tags[i];
                if (i < foundItem->Tags.size() - 1) tagsJoined += L", ";
            }
            HistoryTagsBox().Text(winrt::hstring(tagsJoined));

            // store the original appraisal name to know what to update the new one with
            m_originalAppraisalName = winrt::to_string(foundItem->Name);

            // show dialog
            HistoryDialog().XamlRoot(this->Content().XamlRoot());
            HistoryDialog().ShowAsync();
        }
    }    

    // event handler overloaded function calls the original with mandatory params for WinUI3
    void MainWindow::RenderAppraisalsList(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::Controls::TextChangedEventArgs const&) {
        RenderAppraisalsList();
    }

    // search for tags
    void MainWindow::FilterSearchBox_TextChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::TextChangedEventArgs const&) {
        PopulateTagFilterList(sender.as<TextBox>().Text().c_str());
    }

    // close appraisals
    void MainWindow::CloseAppraisals_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&) {
        AppraisalsView().Visibility(Visibility::Collapsed);
        RecordingView().Visibility(Visibility::Visible);
    }

    winrt::fire_and_forget MainWindow::HistoryDialog_SaveClick(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&) {
        // capture new data
        winrt::hstring newName = HistoryNameBox().Text();
        winrt::hstring newSummary = HistorySummaryBox().Text();
        winrt::hstring newNotes = HistoryNotesBox().Text();
        winrt::hstring newTagsRaw = HistoryTagsBox().Text();

        HistoryDialog().Hide();

        // update the new item
        for (auto it = m_allAppraisals.begin(); it != m_allAppraisals.end(); ++it) {
            if (it->Name == winrt::to_hstring(m_originalAppraisalName)) {
                // found the old entry
                it->Name = newName;
                it->Summary = newSummary;
                it->Notes = newNotes;

                // reparse tags
                it->Tags.clear();
                std::wstring_view tagView = newTagsRaw;
                size_t pos = 0;
                while ((pos = tagView.find(L',')) != std::wstring_view::npos) {
                    it->Tags.push_back(std::wstring(tagView.substr(0, pos)));
                    tagView = tagView.substr(pos + 1);
                    // Trim leading space (optional but good)
                    if (!tagView.empty() && tagView.front() == L' ') tagView.remove_prefix(1);
                }
                if (!tagView.empty()) it->Tags.push_back(std::wstring(tagView));

                break;
            }
        }

        // update UI
        MainWindow::RenderAppraisalsList();

        std::wstringstream txtContent;
        txtContent << L"Appraisal: " << newName.c_str() << L"\n\n";
        txtContent << L"Summary:\n" << newSummary.c_str() << L"\n\n";
        txtContent << L"Notes:\n" << newNotes.c_str() << L"\n\n";
        txtContent << L"Tags: " << newTagsRaw.c_str();

        winrt::hstring fileContent = winrt::hstring(txtContent.str());


        std::wstring filePrefix = MainWindow::getFilenamePrefix(L"%Y-%m-%d_Appraisal_");

        std::wstring fileName = filePrefix + newName.c_str();
        co_await MainWindow::SaveTextToFileAsync(fileName, fileContent);
        co_await MainWindow::SaveAppraisalToJsonAsync(newName, newSummary, newTagsRaw, newNotes);
    }

    void MainWindow::HistoryDialog_CancelClick(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        HistoryDialog().Hide();
    }

    // delete appraisal entry from memory and JSON
    winrt::fire_and_forget MainWindow::HistoryDialog_DeleteClick(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&) {

        // hide immediately
        HistoryDialog().Hide();

        // convert to search for the record
        winrt::hstring targetName = winrt::to_hstring(m_originalAppraisalName);

        // remove from internal memory
        auto it = std::remove_if(m_allAppraisals.begin(), m_allAppraisals.end(),
            [&](const AppraisalItem& app) { return app.Name == targetName; });

        if (it != m_allAppraisals.end()) {
            m_allAppraisals.erase(it, m_allAppraisals.end());
        }

        // remove from JSON on disk
		using namespace winrt::Windows::Storage;
		using namespace winrt::Windows::Data::Json;

		StorageFolder localFolder = ApplicationData::Current().LocalFolder();

		IStorageItem item = co_await localFolder.TryGetItemAsync(L"appraisals.json");

		if (item) {
			StorageFile file = item.as<StorageFile>();
			winrt::hstring jsonString = co_await FileIO::ReadTextAsync(file);
			JsonObject rootObject;

            // parse JSON
			if (JsonObject::TryParse(jsonString, rootObject)) {

                // if the key exists, remove it
				if (rootObject.HasKey(targetName)) {
					rootObject.Remove(targetName);

                    // update the file
					co_await FileIO::WriteTextAsync(file, rootObject.Stringify());
				}
			}
		}

        // re-render UI
        MainWindow::RenderAppraisalsList();
    }
}