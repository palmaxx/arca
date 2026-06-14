import SwiftUI

struct SidebarView: View {
    @Binding var selection: AppSection

    var body: some View {
        List {
            Section("Arca") {
                ForEach(AppSection.allCases) { section in
                    Button {
                        selection = section
                    } label: {
                        Label(section.title, systemImage: section.systemImage)
                            .frame(maxWidth: .infinity, alignment: .leading)
                    }
                    .buttonStyle(.plain)
                }
            }
        }
        .listStyle(.sidebar)
        .navigationTitle("Arca")
    }
}
