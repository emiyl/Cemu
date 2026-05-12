import SwiftUI
import UIKit

// VPAD button flag constants (from Cafe/OS/libs/vpad/vpad.h)
private let VPAD_A:     UInt64 = 0x8000
private let VPAD_B:     UInt64 = 0x4000
private let VPAD_X:     UInt64 = 0x2000
private let VPAD_Y:     UInt64 = 0x1000
private let VPAD_L:     UInt64 = 0x0020
private let VPAD_R:     UInt64 = 0x0010
private let VPAD_ZL:    UInt64 = 0x0080
private let VPAD_ZR:    UInt64 = 0x0040
private let VPAD_PLUS:  UInt64 = 0x0008
private let VPAD_MINUS: UInt64 = 0x0004
private let VPAD_HOME:  UInt64 = 0x0002
private let VPAD_UP:    UInt64 = 0x0200
private let VPAD_DOWN:  UInt64 = 0x0100
private let VPAD_LEFT:  UInt64 = 0x0800
private let VPAD_RIGHT: UInt64 = 0x0400

// MARK: - Reusable button that reports press/release immediately via DragGesture

private struct PadButton: View {
    let label: String
    let flag: UInt64
    var color: Color = .gray
    var width: CGFloat = 48
    var height: CGFloat = 48
    var isCircle: Bool = false

    @State private var pressed = false

    var body: some View {
        Text(label)
            .font(.system(size: 14, weight: .bold))
            .foregroundColor(.white)
            .frame(width: width, height: height)
            .background((pressed ? color : color.opacity(0.55)).animation(nil, value: pressed))
            .clipShape(isCircle ? AnyShape(Circle()) : AnyShape(RoundedRectangle(cornerRadius: 6)))
            .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { _ in
                        guard !pressed else { return }
                        pressed = true
                        IOSTouchInput_ButtonPressed(flag)
                    }
                    .onEnded { _ in
                        pressed = false
                        IOSTouchInput_ButtonReleased(flag)
                    }
            )
    }
}

// MARK: - D-pad

private struct DPad: View {
    private let size: CGFloat = 44

    var body: some View {
        VStack(spacing: 0) {
            PadButton(label: "▲", flag: VPAD_UP,    width: size, height: size)
            HStack(spacing: 0) {
                PadButton(label: "◀", flag: VPAD_LEFT,  width: size, height: size)
                Color.gray.opacity(0.25).frame(width: size, height: size)
                PadButton(label: "▶", flag: VPAD_RIGHT, width: size, height: size)
            }
            PadButton(label: "▼", flag: VPAD_DOWN,  width: size, height: size)
        }
    }
}

// MARK: - Face buttons (diamond layout)

private struct FaceButtons: View {
    private let size: CGFloat = 48

    var body: some View {
        VStack(spacing: 0) {
            PadButton(label: "X", flag: VPAD_X, color: .init(red: 0.6, green: 0.6, blue: 0.9), width: size, height: size, isCircle: true)
            HStack(spacing: 0) {
                PadButton(label: "Y", flag: VPAD_Y, color: .init(red: 0.4, green: 0.7, blue: 0.4), width: size, height: size, isCircle: true)
                Color.clear.frame(width: size, height: size)
                PadButton(label: "A", flag: VPAD_A, color: .init(red: 0.85, green: 0.3, blue: 0.3), width: size, height: size, isCircle: true)
            }
            PadButton(label: "B", flag: VPAD_B, color: .init(red: 0.85, green: 0.65, blue: 0.2), width: size, height: size, isCircle: true)
        }
    }
}

// MARK: - Analog stick

private struct AnalogStick: View {
    let size: CGFloat
    let onPositionChange: (Float, Float) -> Void

    @State private var stickOffset: CGPoint = .zero
    @State private var active = false

    private var maxRadius: CGFloat { size / 2 - 12 }
    private var knobRadius: CGFloat { size / 4 }

    var body: some View {
        ZStack {
            Circle()
                .fill(Color.gray.opacity(0.4))
                .frame(width: size, height: size)

            Circle()
                .fill(active ? Color.white.opacity(0.5) : Color.gray.opacity(0.65))
                .frame(width: knobRadius * 2, height: knobRadius * 2)
                .offset(x: stickOffset.x, y: stickOffset.y)
        }
        .frame(width: size, height: size)
        .gesture(
            DragGesture(minimumDistance: 0)
                .onChanged { value in
                    let dx = value.location.x - size / 2
                    let dy = value.location.y - size / 2
                    let dist = sqrt(dx * dx + dy * dy)
                    let clamped = min(dist, maxRadius)
                    let angle = atan2(dy, dx)
                    let nx = cos(angle) * clamped
                    let ny = sin(angle) * clamped
                    stickOffset = CGPoint(x: nx, y: ny)
                    active = true
                    let norm = clamped / maxRadius
                    onPositionChange(Float(norm * cos(angle)), Float(norm * sin(angle)))
                }
                .onEnded { _ in
                    stickOffset = .zero
                    active = false
                    onPositionChange(0, 0)
                }
        )
    }
}

// MARK: - Main gamepad view

struct OnScreenGamepad: View {
    var body: some View {
        VStack(spacing: 0) {
            // ── Top bar: ZL/L · center buttons · R/ZR ──────────────────────
            HStack(alignment: .center) {
                HStack(spacing: 4) {
                    PadButton(label: "ZL", flag: VPAD_ZL, color: .blue, width: 44, height: 30)
                    PadButton(label: "L",  flag: VPAD_L,  color: .blue, width: 44, height: 30)
                }
                Spacer()
                HStack(spacing: 8) {
                    PadButton(label: "−",  flag: VPAD_MINUS, color: .init(white: 0.35), width: 36, height: 28)
                    PadButton(label: "⌂",  flag: VPAD_HOME,  color: .init(white: 0.35), width: 36, height: 28)
                    PadButton(label: "+",  flag: VPAD_PLUS,  color: .init(white: 0.35), width: 36, height: 28)
                }
                Spacer()
                HStack(spacing: 4) {
                    PadButton(label: "R",  flag: VPAD_R,  color: .blue, width: 44, height: 30)
                    PadButton(label: "ZR", flag: VPAD_ZR, color: .blue, width: 44, height: 30)
                }
            }
            .padding(.horizontal, 16)
            .padding(.top, 8)

            Spacer()

            // ── Bottom row: left controls · right controls ──────────────────
            HStack(alignment: .bottom, spacing: 0) {
                // Left: D-pad above left stick
                VStack(spacing: 20) {
                    DPad()
                    AnalogStick(size: 90) { x, y in
                        IOSTouchInput_SetLeftStick(x, y)
                    }
                }
                .padding(.leading, 16)
                .padding(.bottom, 16)

                Spacer()

                // Right: face buttons above right stick
                VStack(spacing: 20) {
                    FaceButtons()
                    AnalogStick(size: 90) { x, y in
                        IOSTouchInput_SetRightStick(x, y)
                    }
                }
                .padding(.trailing, 16)
                .padding(.bottom, 16)
            }
        }
        .onAppear {
            IOSTouchInput_Initialize()
        }
    }
}

// MARK: - ObjC-visible factory so WindowSystemIOS_GameLoad.mm can create the overlay

@objc(OnScreenGamepadFactory) public class OnScreenGamepadFactory: NSObject {
    @MainActor @objc public static func makeHostingController() -> UIViewController {
        let host = UIHostingController(rootView: OnScreenGamepad())
        host.view.backgroundColor = .clear
        return host
    }
}

// MARK: - AnyShape helper (back-ports the iOS 16 AnyShape to older targets)

private struct AnyShape: Shape, @unchecked Sendable {
    private let _path: @Sendable (CGRect) -> Path
    init<S: Shape>(_ shape: S) { let captured = shape; _path = { captured.path(in: $0) } }
    func path(in rect: CGRect) -> Path { _path(rect) }
}

#Preview {
    OnScreenGamepad()
        .background(Color.black)
}
