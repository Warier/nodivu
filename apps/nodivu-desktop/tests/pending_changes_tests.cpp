// Exercise only native dialogs owned by this test process/window.
// clang-format off
#include <windows.h>
#include <commctrl.h>
// clang-format on
#include "ui/dialogs/pending_changes_dialog.hpp"
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QProcess>
#include <QtTest>
#include <chrono>
#include <thread>

namespace {
struct DialogProbe {
    HWND owner = nullptr;
    HWND dialog = nullptr;
};
BOOL CALLBACK findDialog(HWND candidate, LPARAM parameter) {
    auto& probe = *reinterpret_cast<DialogProbe*>(parameter);
    DWORD pid = 0;
    GetWindowThreadProcessId(candidate, &pid);
    wchar_t name[64]{};
    GetClassNameW(candidate, name, 64);
    if (pid == GetCurrentProcessId() && GetWindow(candidate, GW_OWNER) == probe.owner &&
        wcscmp(name, L"#32770") == 0) {
        probe.dialog = candidate;
        return FALSE;
    }
    return TRUE;
}
bool capture(HWND dialog, const QString& path) {
    RECT bounds{};
    if (!GetWindowRect(dialog, &bounds))
        return false;
    const int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096)
        return false;
    const auto screen = GetWindowDC(dialog);
    const auto memory = CreateCompatibleDC(screen);
    const auto bitmap = CreateCompatibleBitmap(screen, width, height);
    const auto previous = SelectObject(memory, bitmap);
    const bool painted = PrintWindow(dialog, memory, 0) != FALSE;
    SelectObject(memory, previous);
    QImage image(width, height, QImage::Format_RGB32);
    BITMAPINFO format{};
    format.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    format.bmiHeader.biWidth = width;
    format.bmiHeader.biHeight = -height;
    format.bmiHeader.biPlanes = 1;
    format.bmiHeader.biBitCount = 32;
    format.bmiHeader.biCompression = BI_RGB;
    const bool copied = painted && GetDIBits(screen, bitmap, 0, static_cast<UINT>(height),
                                             image.bits(), &format, DIB_RGB_COLORS) != 0;
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(dialog, screen);
    if (!copied)
        return false;
    const auto first = image.pixel(0, 0);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            if (image.pixel(x, y) != first)
                return image.save(path);
    return false; // A uniform/blank capture is not visual evidence.
}
} // namespace
class PendingChangesTests final : public QObject {
    Q_OBJECT
  private slots:
    void nativeChoices_data() {
        QTest::addColumn<int>("action");
        QTest::addColumn<int>("button");
        QTest::newRow("close-continue") << int(nodivu::PendingChangesAction::Close) << 100;
        QTest::newRow("close-discard") << int(nodivu::PendingChangesAction::Close) << 101;
        QTest::newRow("open-cancel") << int(nodivu::PendingChangesAction::Open) << 100;
        QTest::newRow("open-escape") << int(nodivu::PendingChangesAction::Open) << int(VK_ESCAPE);
        QTest::newRow("restore-close") << int(nodivu::PendingChangesAction::Restore) << IDCANCEL;
    }
    void nativeChoices() {
        QFETCH(int, action);
        QFETCH(int, button);
        QWidget owner;
        owner.resize(800, 500);
        owner.setWindowTitle(QStringLiteral("Nodivu — teste de confirmação"));
        owner.show();
        QTest::qWait(100);
        DialogProbe probe{reinterpret_cast<HWND>(owner.winId()), nullptr};
        const DWORD uiThread = GetCurrentThreadId();
        const auto context = static_cast<nodivu::PendingChangesAction>(action);
        const auto expected = nodivu::pendingChangesText(context);
        const auto evidence = qEnvironmentVariable("NODIVU_WORKSPACE_SCREENSHOTS");
        QString cancelText, proceedText;
        bool screenshot = evidence.isEmpty(), completed = false;
        std::jthread actor([&] {
            const auto deadline = GetTickCount64() + 5000;
            while (!probe.dialog && GetTickCount64() < deadline) {
                EnumThreadWindows(uiThread, findDialog, reinterpret_cast<LPARAM>(&probe));
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            if (!probe.dialog) {
                QMetaObject::invokeMethod(
                    qApp,
                    [] {
                        if (auto* fallback =
                                qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
                            fallback->reject();
                    },
                    Qt::QueuedConnection);
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            // TaskDialog exposes command links through UI Automation rather
            // than child HWNDs. Read only this owned dialog via the existing actor.
            QProcess reader;
            reader.start(QDir(QCoreApplication::applicationDirPath())
                             .filePath(QStringLiteral("windows_accessibility_actor.exe")),
                         {QString::number(reinterpret_cast<quintptr>(probe.dialog)),
                          QString::number(GetCurrentProcessId())});
            if (reader.waitForStarted(3000) && reader.waitForFinished(5000)) {
                const auto output = reader.readAllStandardOutput();
                if (!evidence.isEmpty()) {
                    QFile log(QDir(evidence).filePath(
                        QStringLiteral("pending-tree-%1-%2.json").arg(action).arg(button)));
                    if (log.open(QIODevice::WriteOnly))
                        log.write(output);
                }
                const auto tree = QJsonDocument::fromJson(output).object();
                for (const auto& entry : tree.value(QStringLiteral("before")).toArray()) {
                    const auto control = entry.toObject();
                    const auto name = control.value(QStringLiteral("name")).toString();
                    if (name == expected.cancel) {
                        cancelText = name;
                    }
                    if (name == expected.proceed)
                        proceedText = name;
                }
            }
            RedrawWindow(probe.dialog, nullptr, nullptr, RDW_UPDATENOW | RDW_ALLCHILDREN);
            if (!evidence.isEmpty())
                screenshot = capture(
                    probe.dialog,
                    QDir(evidence).filePath(
                        QStringLiteral("pending-changes-%1-%2.png").arg(action).arg(button)));
            if (button == IDCANCEL)
                PostMessageW(probe.dialog, WM_CLOSE, 0, 0);
            else if (button == 100 || button == VK_ESCAPE)
                PostMessageW(probe.dialog, WM_KEYDOWN, button == 100 ? VK_RETURN : VK_ESCAPE, 0);
            else
                PostMessageW(probe.dialog, TDM_CLICK_BUTTON, static_cast<WPARAM>(button), 0);
            const auto closeDeadline = GetTickCount64() + 2000;
            while (IsWindow(probe.dialog) && GetTickCount64() < closeDeadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            completed = !IsWindow(probe.dialog);
            if (!completed)
                PostMessageW(probe.dialog, WM_CLOSE, 0, 0);
        });
        const bool approved = nodivu::confirmPendingChanges(&owner, context);
        actor.join();
        QVERIFY2(probe.dialog, "Expected a real native Windows task dialog, not a fallback.");
        QCOMPARE(cancelText, expected.cancel);
        QCOMPARE(proceedText, expected.proceed);
        QVERIFY(screenshot);
        QVERIFY(completed);
        QCOMPARE(approved, button == 101);
        QVERIFY(owner.isEnabled());
    }
};
QTEST_MAIN(PendingChangesTests)
#include "pending_changes_tests.moc"
