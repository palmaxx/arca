import Combine
import Foundation

@MainActor
final class ShellViewModel: ObservableObject {
    @Published var selection: AppSection = .home
    @Published var libraries: [LibraryInfo] = []
    @Published var selectedLibrary: LibraryInfo?
    @Published var children = LibraryChildren()
    @Published var browse = BrowseResult()
    @Published var selectedBrowseFilter = "all"
    @Published var continueWatching: [ProgressEntry] = []
    @Published var queue = QueueSnapshot()
    @Published var searchText = ""
    @Published var searchResults: [MediaInfo] = []
    @Published var selectedMedia: MediaInfo?
    @Published var sortOrder: MediaSortOrder = .nameAscending
    @Published var statusText = "Stub core"

    private let core: any ArcaCoreClient

    init(core: any ArcaCoreClient = ArcaCoreClientFactory.make()) {
        self.core = core
    }

    func load() async {
        do {
            libraries = try await core.loadLibraries()
            await loadBrowse()
            continueWatching = try await core.continueWatching(limit: 12)
            queue = try await core.queueSnapshot()
            if selectedLibrary == nil, let first = libraries.first {
                await selectLibrary(first)
            }
            statusText = core.statusDescription
        } catch {
            statusText = error.localizedDescription
        }
    }

    func selectLibrary(_ library: LibraryInfo) async {
        selectedLibrary = library
        await loadChildren(folder: "")
    }

    func loadChildren(folder: String) async {
        guard let library = selectedLibrary else { return }
        do {
            children = try await core.loadChildren(
                libraryId: library.id,
                folder: folder,
                sort: sortOrder
            )
        } catch {
            statusText = error.localizedDescription
        }
    }

    func runSearch() async {
        let trimmed = searchText.trimmingCharacters(in: .whitespacesAndNewlines)
        guard trimmed.count >= 2 else {
            searchResults = []
            return
        }

        do {
            searchResults = try await core.search(
                trimmed,
                libraryId: selectedLibrary?.id,
                limit: 80
            )
        } catch {
            statusText = error.localizedDescription
        }
    }

    func loadBrowse(filter: String? = nil) async {
        if let filter {
            selectedBrowseFilter = filter
        }
        do {
            browse = try await core.browse(
                filter: selectedBrowseFilter,
                rowLimit: 8,
                itemLimit: 24
            )
            selectedBrowseFilter = browse.selectedFilter
        } catch {
            statusText = error.localizedDescription
        }
    }

    func play(_ media: MediaInfo) async {
        selectedMedia = media
        selection = .player
        statusText = "Ready: \(media.displayTitle)"
    }
}
