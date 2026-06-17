import SwiftUI

struct BrowseView: View {
    @ObservedObject var model: ShellViewModel

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            header
            if model.browse.sections.isEmpty {
                emptyState
            } else {
                ScrollView {
                    LazyVStack(alignment: .leading, spacing: 24) {
                        ForEach(model.browse.sections) { section in
                            BrowseSectionView(section: section, model: model)
                        }
                    }
                    .padding(24)
                }
            }
        }
    }

    private var header: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack {
                Text("Browse")
                    .font(.largeTitle.weight(.semibold))
                Spacer()
                Text(model.statusText)
                    .foregroundStyle(.secondary)
                    .lineLimit(1)
            }

            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 8) {
                    ForEach(model.browse.filters) { filter in
                        Button {
                            Task { await model.loadBrowse(filter: filter.key) }
                        } label: {
                            Text(filter.display)
                                .font(.subheadline.weight(filter.selected ? .semibold : .regular))
                                .lineLimit(1)
                                .padding(.horizontal, 12)
                                .padding(.vertical, 7)
                                .background {
                                    RoundedRectangle(cornerRadius: 8)
                                        .fill(filter.selected ? Color.accentColor.opacity(0.18) : Color.secondary.opacity(0.10))
                                }
                                .overlay {
                                    RoundedRectangle(cornerRadius: 8)
                                        .stroke(filter.selected ? Color.accentColor.opacity(0.55) : Color.secondary.opacity(0.18))
                                }
                        }
                        .buttonStyle(.plain)
                    }
                }
            }
        }
        .padding(.horizontal, 24)
        .padding(.vertical, 18)
        .background(.regularMaterial)
    }

    private var emptyState: some View {
        VStack(spacing: 12) {
            Image(systemName: "square.grid.2x2")
                .font(.system(size: 44))
                .foregroundStyle(.secondary)
            Text("Nothing to browse in this filter yet")
                .font(.headline)
            Text("Add or scan a library, then refresh.")
                .foregroundStyle(.secondary)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
}

private struct BrowseSectionView: View {
    let section: BrowseSection
    @ObservedObject var model: ShellViewModel

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text(section.title)
                .font(.title2.weight(.semibold))

            ForEach(section.rows) { row in
                VStack(alignment: .leading, spacing: 8) {
                    Text(row.title)
                        .font(.subheadline)
                        .foregroundStyle(.secondary)

                    ScrollView(.horizontal, showsIndicators: false) {
                        LazyHStack(spacing: 12) {
                            ForEach(row.entries) { media in
                                Button {
                                    Task { await model.play(media) }
                                } label: {
                                    BrowseMediaCard(media: media)
                                }
                                .buttonStyle(.plain)
                            }
                        }
                        .padding(.bottom, 2)
                    }
                }
            }
        }
    }
}

private struct BrowseMediaCard: View {
    let media: MediaInfo

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            ZStack(alignment: .bottomTrailing) {
                RoundedRectangle(cornerRadius: 8)
                    .fill(.quaternary)
                Image(systemName: media.mode == "online" ? "link" : "film")
                    .font(.system(size: 38))
                    .foregroundStyle(.secondary)
                Image(systemName: "play.circle.fill")
                    .font(.title2)
                    .symbolRenderingMode(.hierarchical)
                    .padding(8)
            }
            .frame(width: 168, height: 150)

            Text(media.displayTitle)
                .font(.headline)
                .foregroundStyle(.primary)
                .lineLimit(2)
                .frame(width: 168, alignment: .leading)

            Text(media.libraryName ?? media.subtitle)
                .font(.caption)
                .foregroundStyle(.secondary)
                .lineLimit(1)
                .frame(width: 168, alignment: .leading)
        }
        .frame(width: 184, height: 232, alignment: .topLeading)
        .padding(8)
        .background {
            RoundedRectangle(cornerRadius: 8)
                .fill(Color.secondary.opacity(0.08))
        }
        .overlay {
            RoundedRectangle(cornerRadius: 8)
                .stroke(Color.secondary.opacity(0.16))
        }
    }
}
