import SwiftUI

struct PlayerPlaceholderView: View {
    @ObservedObject var model: ShellViewModel

    var body: some View {
        VStack(spacing: 16) {
            ZStack {
                Rectangle()
                    .fill(.black)
                VStack(spacing: 12) {
                    Image(systemName: "play.rectangle")
                        .font(.system(size: 56))
                        .foregroundStyle(.white.opacity(0.85))
                    Text(model.selectedMedia?.displayTitle ?? "No media selected")
                        .font(.title2.weight(.semibold))
                        .foregroundStyle(.white)
                    Text("Metal video surface will replace this view.")
                        .foregroundStyle(.white.opacity(0.7))
                }
            }
            .aspectRatio(16 / 9, contentMode: .fit)

            HStack(spacing: 16) {
                Button {
                } label: {
                    Label("Previous", systemImage: "backward.fill")
                }
                Button {
                } label: {
                    Label("Play", systemImage: "play.fill")
                }
                .keyboardShortcut(.space, modifiers: [])
                Button {
                } label: {
                    Label("Next", systemImage: "forward.fill")
                }
                Spacer()
                Text(model.statusText)
                    .foregroundStyle(.secondary)
            }
        }
        .padding(24)
    }
}
