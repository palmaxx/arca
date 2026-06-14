import SwiftUI

struct SearchView: View {
    @ObservedObject var model: ShellViewModel

    var body: some View {
        VStack(spacing: 0) {
            HStack {
                Image(systemName: "magnifyingglass")
                    .foregroundStyle(.secondary)
                TextField("Search library", text: $model.searchText)
                    .textFieldStyle(.plain)
                    .onSubmit {
                        Task { await model.runSearch() }
                    }
                Button {
                    Task { await model.runSearch() }
                } label: {
                    Label("Search", systemImage: "arrow.right")
                }
            }
            .padding(12)
            .background(.regularMaterial)

            List(model.searchResults) { media in
                Button {
                    Task { await model.play(media) }
                } label: {
                    MediaRow(media: media)
                }
            }
        }
    }
}
