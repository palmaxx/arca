import Foundation

enum AppSection: String, CaseIterable, Identifiable, Hashable {
    case home
    case browse
    case library
    case search
    case player
    case queue
    case settings

    var id: String { rawValue }

    var title: String {
        switch self {
        case .home: "Home"
        case .browse: "Browse"
        case .library: "Library"
        case .search: "Search"
        case .player: "Player"
        case .queue: "Queue"
        case .settings: "Settings"
        }
    }

    var systemImage: String {
        switch self {
        case .home: "house"
        case .browse: "square.grid.2x2"
        case .library: "rectangle.stack"
        case .search: "magnifyingglass"
        case .player: "play.rectangle"
        case .queue: "list.bullet"
        case .settings: "gearshape"
        }
    }
}

enum MediaSortOrder: Int32, CaseIterable, Identifiable {
    case nameAscending = 0
    case dateDescending = 1

    var id: Int32 { rawValue }

    var title: String {
        switch self {
        case .nameAscending: "Name"
        case .dateDescending: "Recent"
        }
    }
}
