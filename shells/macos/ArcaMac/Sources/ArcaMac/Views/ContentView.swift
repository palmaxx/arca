import SwiftUI

struct ContentView: View {
    @StateObject private var model = ShellViewModel()

    var body: some View {
        NavigationSplitView {
            SidebarView(selection: $model.selection)
        } detail: {
            Group {
                switch model.selection {
                case .home:
                    HomeView(model: model)
                case .browse:
                    BrowseView(model: model)
                case .library:
                    LibraryView(model: model)
                case .search:
                    SearchView(model: model)
                case .player:
                    PlayerPlaceholderView(model: model)
                case .queue:
                    QueueView(model: model)
                case .settings:
                    SettingsView(model: model)
                }
            }
            .navigationTitle(model.selection.title)
            .toolbar {
                ToolbarItemGroup {
                    Button {
                        Task { await model.load() }
                    } label: {
                        Label("Refresh", systemImage: "arrow.clockwise")
                    }
                }
            }
        }
        .task {
            await model.load()
        }
    }
}

private struct HomeView: View {
    @ObservedObject var model: ShellViewModel

    var body: some View {
        ScrollView {
            LazyVStack(alignment: .leading, spacing: 24) {
                sectionTitle("Continue Watching")
                if model.continueWatching.isEmpty {
                    VStack(spacing: 8) {
                        Image(systemName: "play.circle")
                            .font(.largeTitle)
                            .foregroundStyle(.secondary)
                        Text("No progress yet")
                            .foregroundStyle(.secondary)
                    }
                    .frame(maxWidth: .infinity, minHeight: 140)
                } else {
                    LazyVGrid(columns: [GridItem(.adaptive(minimum: 220), spacing: 12)], spacing: 12) {
                        ForEach(model.continueWatching) { entry in
                            Button {
                                Task { await model.play(entry.media) }
                            } label: {
                                VStack(alignment: .leading, spacing: 8) {
                                    Text(entry.media.displayTitle)
                                        .font(.headline)
                                        .lineLimit(2)
                                    Text(entry.media.subtitle)
                                        .font(.subheadline)
                                        .foregroundStyle(.secondary)
                                        .lineLimit(1)
                                    ProgressView(value: entry.progressFraction)
                                }
                                .frame(maxWidth: .infinity, alignment: .leading)
                                .padding()
                            }
                            .buttonStyle(.bordered)
                        }
                    }
                }

                sectionTitle("Libraries")
                LazyVGrid(columns: [GridItem(.adaptive(minimum: 240), spacing: 12)], spacing: 12) {
                    ForEach(model.libraries) { library in
                        Button {
                            Task {
                                model.selection = .library
                                await model.selectLibrary(library)
                            }
                        } label: {
                            Label {
                                VStack(alignment: .leading) {
                                    Text(library.name)
                                        .font(.headline)
                                    Text(library.rootPath)
                                        .font(.subheadline)
                                        .foregroundStyle(.secondary)
                                        .lineLimit(1)
                                }
                            } icon: {
                                Image(systemName: library.isOnline ? "cloud" : "externaldrive")
                            }
                            .frame(maxWidth: .infinity, alignment: .leading)
                            .padding()
                        }
                        .buttonStyle(.bordered)
                    }
                }
            }
            .padding(24)
        }
    }

    private func sectionTitle(_ title: String) -> some View {
        Text(title)
            .font(.title2.weight(.semibold))
    }
}
