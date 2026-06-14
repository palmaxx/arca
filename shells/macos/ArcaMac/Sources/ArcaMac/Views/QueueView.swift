import SwiftUI

struct QueueView: View {
    @ObservedObject var model: ShellViewModel

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            HStack {
                Text(model.queue.shuffle ? "Shuffle on" : "Shuffle off")
                    .foregroundStyle(.secondary)
                Spacer()
                Text("\(model.queue.items.count) items")
                    .foregroundStyle(.secondary)
            }
            .padding()

            List(Array(model.queue.items.enumerated()), id: \.element.id) { index, media in
                Button {
                    Task { await model.play(media) }
                } label: {
                    HStack {
                        Image(systemName: index == model.queue.currentIndex ? "play.fill" : "circle")
                            .foregroundStyle(index == model.queue.currentIndex ? Color.accentColor : Color.secondary)
                        MediaRow(media: media)
                    }
                }
            }
        }
    }
}
