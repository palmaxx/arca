import SwiftUI

struct SettingsView: View {
    @ObservedObject var model: ShellViewModel

    var body: some View {
        Form {
            Section("Core") {
                LabeledContent("Backend", value: model.statusText)
                LabeledContent("ABI", value: "Flat C JSON boundary")
                LabeledContent("Video Surface", value: "Metal pending")
            }

            Section("Validation") {
                Text("Build the package on macOS, confirm the sidebar/detail shell opens, then enable ARCA_NATIVE_CORE once arca_core is linked.")
                    .foregroundStyle(.secondary)
            }
        }
        .formStyle(.grouped)
        .padding()
    }
}
