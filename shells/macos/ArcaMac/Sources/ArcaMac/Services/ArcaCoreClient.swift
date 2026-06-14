import Foundation

protocol ArcaCoreClient: AnyObject {
    var statusDescription: String { get }

    func loadLibraries() async throws -> [LibraryInfo]
    func loadChildren(libraryId: Int64, folder: String, sort: MediaSortOrder) async throws -> LibraryChildren
    func search(_ query: String, libraryId: Int64?, limit: Int32) async throws -> [MediaInfo]
    func continueWatching(limit: Int32) async throws -> [ProgressEntry]
    func queueSnapshot() async throws -> QueueSnapshot
}

enum ArcaCoreClientFactory {
    static func make() -> any ArcaCoreClient {
        #if ARCA_NATIVE_CORE
        do {
            return try NativeArcaCoreClient()
        } catch {
            return StubArcaCoreClient(statusDescription: error.localizedDescription)
        }
        #else
        return StubArcaCoreClient()
        #endif
    }
}

enum ArcaClientError: Error, LocalizedError {
    case openFailed(String)
    case nullString(String)
    case decodeFailed(String)

    var errorDescription: String? {
        switch self {
        case .openFailed(let path): "Could not open Arca database at \(path)"
        case .nullString(let function): "\(function) returned a null string"
        case .decodeFailed(let details): "Could not decode core JSON: \(details)"
        }
    }
}
