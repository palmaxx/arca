import SwiftUI

struct LibraryView: View {
    @ObservedObject var model: ShellViewModel

    var body: some View {
        HSplitView {
            List(model.libraries, selection: bindingForSelectedLibrary) { library in
                Label {
                    VStack(alignment: .leading) {
                        Text(library.name)
                        Text(library.rootPath)
                            .font(.caption)
                            .foregroundStyle(.secondary)
                            .lineLimit(1)
                    }
                } icon: {
                    Image(systemName: library.isOnline ? "cloud" : "externaldrive")
                }
                .tag(library.id)
            }
            .frame(minWidth: 220, idealWidth: 260)

            VStack(spacing: 0) {
                HStack {
                    Text(model.selectedLibrary?.name ?? "Library")
                        .font(.title2.weight(.semibold))
                    Spacer()
                    Picker("Sort", selection: $model.sortOrder) {
                        ForEach(MediaSortOrder.allCases) { order in
                            Text(order.title).tag(order)
                        }
                    }
                    .pickerStyle(.segmented)
                    .frame(width: 180)
                    .onChange(of: model.sortOrder) { _ in
                        Task { await model.loadChildren(folder: model.children.folderRelPath) }
                    }
                }
                .padding()

                List {
                    if !model.children.folderRelPath.isEmpty {
                        Button {
                            Task { await model.loadChildren(folder: "") }
                        } label: {
                            Label("Back", systemImage: "chevron.left")
                        }
                    }

                    if !model.children.folders.isEmpty {
                        Section("Folders") {
                            ForEach(model.children.folders) { folder in
                                Button {
                                    Task { await model.loadChildren(folder: folder.relPath) }
                                } label: {
                                    Label(folder.title, systemImage: "folder")
                                }
                            }
                        }
                    }

                    Section("Media") {
                        ForEach(model.children.media) { media in
                            Button {
                                Task { await model.play(media) }
                            } label: {
                                MediaRow(media: media)
                            }
                        }
                    }
                }
            }
            .frame(minWidth: 520)
        }
    }

    private var bindingForSelectedLibrary: Binding<Int64?> {
        Binding {
            model.selectedLibrary?.id
        } set: { newValue in
            guard let newValue, let library = model.libraries.first(where: { $0.id == newValue }) else {
                return
            }
            Task { await model.selectLibrary(library) }
        }
    }
}

struct MediaRow: View {
    let media: MediaInfo

    var body: some View {
        Label {
            VStack(alignment: .leading) {
                Text(media.displayTitle)
                    .lineLimit(1)
                Text(media.subtitle)
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .lineLimit(1)
            }
        } icon: {
            Image(systemName: media.mode == "online" ? "link" : "film")
        }
        .padding(.vertical, 4)
    }
}
