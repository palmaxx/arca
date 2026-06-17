import Foundation

struct LibraryInfo: Identifiable, Codable, Hashable {
    let id: Int64
    let name: String
    let rootPath: String
    let mode: String
    let itemCount: Int64

    var isOnline: Bool { mode == "online" }
}

struct FolderInfo: Identifiable, Codable, Hashable {
    let relPath: String
    let title: String

    var id: String { relPath }
}

struct MediaInfo: Identifiable, Codable, Hashable {
    let id: String
    let fileName: String
    let relPath: String
    let folderRelPath: String
    let size: Int64
    let modifiedUtc: Int64
    let addedUtc: Int64
    let title: String?
    let year: Int?
    let season: Int?
    let episode: Int?
    let groupKey: String?
    let libraryId: Int64
    let libraryName: String?
    let mode: String?
    let isCurrent: Bool?

    var displayTitle: String {
        guard let title, !title.isEmpty else {
            return fileName.replacingOccurrences(of: "\\.[^.]+$", with: "", options: .regularExpression)
        }
        if let season, let episode {
            return String(format: "%@ S%02dE%02d", title, season, episode)
        }
        if let year {
            return "\(title) (\(year))"
        }
        return title
    }

    var subtitle: String {
        if let libraryName, !libraryName.isEmpty {
            return folderRelPath.isEmpty ? libraryName : "\(libraryName) / \(folderRelPath)"
        }
        return folderRelPath.isEmpty ? "Video" : folderRelPath
    }
}

struct LibraryChildren: Codable, Hashable {
    var libraryId: Int64 = 0
    var folderRelPath: String = ""
    var folders: [FolderInfo] = []
    var media: [MediaInfo] = []
}

struct ProgressEntry: Identifiable, Codable, Hashable {
    let media: MediaInfo
    let positionSeconds: Double
    let durationSeconds: Double
    let lastUpdatedUtc: Int64

    var id: String { media.id }

    var progressFraction: Double {
        guard durationSeconds > 0 else { return 0 }
        return min(max(positionSeconds / durationSeconds, 0), 1)
    }
}

struct QueueSnapshot: Codable, Hashable {
    var items: [MediaInfo] = []
    var currentIndex: Int = -1
    var shuffle: Bool = false

    var currentItem: MediaInfo? {
        guard items.indices.contains(currentIndex) else { return nil }
        return items[currentIndex]
    }
}

struct BrowseFilter: Identifiable, Codable, Hashable {
    let key: String
    let name: String
    let count: Int64
    let selected: Bool

    var id: String { key }
    var display: String { "\(name) (\(count))" }
}

struct BrowseRow: Identifiable, Codable, Hashable {
    let title: String
    let entries: [MediaInfo]

    var id: String { title }
}

struct BrowseSection: Identifiable, Codable, Hashable {
    let kind: String
    let title: String
    let rows: [BrowseRow]

    var id: String { "\(kind)-\(title)" }
}

struct BrowseResult: Codable, Hashable {
    var selectedFilter: String = "all"
    var filters: [BrowseFilter] = []
    var sections: [BrowseSection] = []
}
