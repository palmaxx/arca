import Foundation

final class StubArcaCoreClient: ArcaCoreClient {
    let statusDescription: String

    private let libraries: [LibraryInfo]
    private let folders: [FolderInfo]
    private let media: [MediaInfo]

    init(statusDescription: String = "Stub core: SwiftUI shell only") {
        self.statusDescription = statusDescription
        libraries = [
            LibraryInfo(id: 1, name: "Local Library", rootPath: "~/Movies", mode: "offline", itemCount: 2),
            LibraryInfo(id: 2, name: "Online Watchlist", rootPath: "https://example.invalid/arca", mode: "online", itemCount: 1)
        ]
        folders = [
            FolderInfo(relPath: "Films", title: "Films"),
            FolderInfo(relPath: "Series", title: "Series"),
            FolderInfo(relPath: "Concerts", title: "Concerts")
        ]
        media = [
            MediaInfo(id: "stub-1", fileName: "North Window.mkv", relPath: "Films/North Window.mkv", folderRelPath: "Films", size: 2_400_000_000, modifiedUtc: 0, addedUtc: 0, title: "North Window", year: 2024, season: nil, episode: nil, groupKey: nil, libraryId: 1, libraryName: "Local Library", mode: "offline", isCurrent: false),
            MediaInfo(id: "stub-2", fileName: "Episode 01.mp4", relPath: "Series/Episode 01.mp4", folderRelPath: "Series", size: 900_000_000, modifiedUtc: 0, addedUtc: 0, title: "Sample Show", year: nil, season: 1, episode: 1, groupKey: "sample-show", libraryId: 1, libraryName: "Local Library", mode: "offline", isCurrent: false),
            MediaInfo(id: "stub-3", fileName: "Remote Sample", relPath: "Remote Sample", folderRelPath: "", size: 0, modifiedUtc: 0, addedUtc: 0, title: "Remote Sample", year: nil, season: nil, episode: nil, groupKey: "remote-sample", libraryId: 2, libraryName: "Online Watchlist", mode: "online", isCurrent: false)
        ]
    }

    func loadLibraries() async throws -> [LibraryInfo] {
        libraries
    }

    func loadChildren(libraryId: Int64, folder: String, sort: MediaSortOrder) async throws -> LibraryChildren {
        let scoped = media.filter { $0.libraryId == libraryId && (folder.isEmpty || $0.relPath == folder) }
        let ordered = sort == .nameAscending
            ? scoped.sorted { $0.displayTitle.localizedStandardCompare($1.displayTitle) == .orderedAscending }
            : Array(scoped.reversed())
        return LibraryChildren(
            libraryId: libraryId,
            folderRelPath: folder,
            folders: folder.isEmpty ? folders : [],
            media: Array(ordered)
        )
    }

    func search(_ query: String, libraryId: Int64?, limit: Int32) async throws -> [MediaInfo] {
        let lowered = query.lowercased()
        return media
            .filter { libraryId == nil || $0.libraryId == libraryId }
            .filter { $0.displayTitle.lowercased().contains(lowered) || $0.relPath.lowercased().contains(lowered) }
            .prefix(Int(limit))
            .map { $0 }
    }

    func continueWatching(limit: Int32) async throws -> [ProgressEntry] {
        media.prefix(Int(limit)).enumerated().map { index, item in
            ProgressEntry(
                media: item,
                positionSeconds: Double(index + 1) * 480,
                durationSeconds: 5400,
                lastUpdatedUtc: 0
            )
        }
    }

    func queueSnapshot() async throws -> QueueSnapshot {
        QueueSnapshot(items: media, currentIndex: 0, shuffle: false)
    }
}
