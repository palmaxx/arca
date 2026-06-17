#if ARCA_NATIVE_CORE
import CArcaCore
import Foundation

final class NativeArcaCoreClient: ArcaCoreClient {
    let statusDescription: String

    private let db: OpaquePointer
    private let queue: OpaquePointer?
    private let decoder = JSONDecoder()

    init(databaseURL: URL? = nil) throws {
        let url: URL
        if let databaseURL {
            url = databaseURL
        } else {
            url = try Self.defaultDatabaseURL()
        }
        try FileManager.default.createDirectory(
            at: url.deletingLastPathComponent(),
            withIntermediateDirectories: true
        )

        guard let opened = arca_db_open(url.path) else {
            throw ArcaClientError.openFailed(url.path)
        }

        db = opened
        queue = arca_queue_create(opened)
        statusDescription = "Native core: \(url.path)"
    }

    deinit {
        if let queue {
            arca_queue_destroy(queue)
        }
        arca_db_close(db)
    }

    func loadLibraries() async throws -> [LibraryInfo] {
        try decode([LibraryInfo].self, from: takeString(arca_library_list_json(db), "arca_library_list_json"))
    }

    func loadChildren(libraryId: Int64, folder: String, sort: MediaSortOrder) async throws -> LibraryChildren {
        try decode(
            LibraryChildren.self,
            from: takeString(
                arca_library_children_json(db, libraryId, folder, sort.rawValue),
                "arca_library_children_json"
            )
        )
    }

    func search(_ query: String, libraryId: Int64?, limit: Int32) async throws -> [MediaInfo] {
        try decode(
            [MediaInfo].self,
            from: takeString(
                arca_media_search_json(db, query, libraryId ?? 0, limit),
                "arca_media_search_json"
            )
        )
    }

    func browse(filter: String?, rowLimit: Int32, itemLimit: Int32) async throws -> BrowseResult {
        try decode(
            BrowseResult.self,
            from: takeString(
                arca_media_browse_json(db, filter ?? "all", rowLimit, itemLimit),
                "arca_media_browse_json"
            )
        )
    }

    func continueWatching(limit: Int32) async throws -> [ProgressEntry] {
        try decode(
            [ProgressEntry].self,
            from: takeString(
                arca_progress_continue_watching_json(db, limit),
                "arca_progress_continue_watching_json"
            )
        )
    }

    func queueSnapshot() async throws -> QueueSnapshot {
        guard let queue else { return QueueSnapshot() }
        return try decode(
            QueueSnapshot.self,
            from: takeString(arca_queue_list_json(queue), "arca_queue_list_json")
        )
    }

    private func takeString(_ pointer: UnsafeMutablePointer<CChar>?, _ function: String) throws -> String {
        guard let pointer else {
            throw ArcaClientError.nullString(function)
        }
        defer { arca_string_free(pointer) }
        return String(cString: pointer)
    }

    private func decode<T: Decodable>(_ type: T.Type, from json: String) throws -> T {
        guard let data = json.data(using: .utf8) else {
            throw ArcaClientError.decodeFailed("invalid UTF-8")
        }

        do {
            return try decoder.decode(T.self, from: data)
        } catch {
            throw ArcaClientError.decodeFailed(error.localizedDescription)
        }
    }

    private static func defaultDatabaseURL() throws -> URL {
        let base = try FileManager.default.url(
            for: .applicationSupportDirectory,
            in: .userDomainMask,
            appropriateFor: nil,
            create: true
        )
        return base.appendingPathComponent("Arca/arca.db", isDirectory: false)
    }
}
#endif
