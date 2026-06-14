import SwiftUI

@main
struct ArcaMacApp: App {
    var body: some Scene {
        WindowGroup("Arca", id: "main") {
            ContentView()
                .frame(minWidth: 980, minHeight: 640)
        }
        .commands {
            SidebarCommands()
        }
    }
}
