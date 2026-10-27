// External UI Automation client for the explicitly owned acceptance window.
// WIN32_LEAN_AND_MEAN omits COM; import its declarations before UI Automation.
// clang-format off
#include <windows.h>
#include <objbase.h>
#include <UIAutomation.h>
#include <wrl/client.h>
// clang-format on
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QThread>
#include <stdexcept>

namespace {
using Microsoft::WRL::ComPtr;
void check(HRESULT result) {
    if (FAILED(result))
        throw std::runtime_error("Windows UI Automation call failed");
}
QString name(IUIAutomationElement* element) {
    BSTR text = nullptr;
    check(element->get_CurrentName(&text));
    const auto result =
        text ? QString::fromWCharArray(text, static_cast<qsizetype>(SysStringLen(text)))
             : QString();
    SysFreeString(text);
    return result;
}
QJsonArray controls(IUIAutomationElement* root, IUIAutomation* automation) {
    ComPtr<IUIAutomationCondition> condition;
    check(automation->get_ControlViewCondition(&condition));
    ComPtr<IUIAutomationElementArray> elements;
    check(root->FindAll(TreeScope_Subtree, condition.Get(), &elements));
    int count = 0;
    check(elements->get_Length(&count));
    if (count < 1 || count > 4096)
        throw std::runtime_error("Unexpected accessibility tree size");
    QJsonArray result;
    for (int index = 0; index < count; ++index) {
        ComPtr<IUIAutomationElement> element;
        check(elements->GetElement(index, &element));
        CONTROLTYPEID type = 0;
        BOOL enabled = FALSE, focusable = FALSE;
        check(element->get_CurrentControlType(&type));
        check(element->get_CurrentIsEnabled(&enabled));
        check(element->get_CurrentIsKeyboardFocusable(&focusable));
        result.append(QJsonObject{{QStringLiteral("name"), name(element.Get())},
                                  {QStringLiteral("type"), type},
                                  {QStringLiteral("enabled"), enabled != FALSE},
                                  {QStringLiteral("keyboard_focusable"), focusable != FALSE}});
    }
    return result;
}
void invoke(IUIAutomationElement* root, IUIAutomation* automation, const QString& text) {
    VARIANT value;
    VariantInit(&value);
    value.vt = VT_BSTR;
    value.bstrVal = SysAllocString(reinterpret_cast<const wchar_t*>(text.utf16()));
    if (!value.bstrVal)
        throw std::runtime_error("BSTR allocation failed");
    ComPtr<IUIAutomationCondition> condition;
    const auto result = automation->CreatePropertyCondition(UIA_NamePropertyId, value, &condition);
    VariantClear(&value);
    check(result);
    ComPtr<IUIAutomationElement> element;
    check(root->FindFirst(TreeScope_Subtree, condition.Get(), &element));
    if (!element)
        throw std::runtime_error("Named accessible control not found");
    ComPtr<IUIAutomationInvokePattern> pattern;
    check(element->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&pattern)));
    check(pattern->Invoke());
}
} // namespace
int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    const auto args = application.arguments();
    if (args.size() < 3 || args.size() > 4)
        return 2;
    bool valid = false;
    const auto handle = args[1].toULongLong(&valid);
    if (!valid || handle == 0)
        return 2;
    const auto window = reinterpret_cast<HWND>(static_cast<quintptr>(handle));
    const auto pid = args[2].toUInt(&valid);
    DWORD owner = 0;
    GetWindowThreadProcessId(window, &owner);
    if (!valid || pid == 0 || owner != pid || !IsWindow(window))
        return 2;
    const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initialized))
        return 3;
    QJsonObject evidence;
    int code = 1;
    try {
        ComPtr<IUIAutomation> automation;
        check(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                               IID_PPV_ARGS(&automation)));
        ComPtr<IUIAutomationElement> root;
        check(automation->ElementFromHandle(window, &root));
        evidence.insert(QStringLiteral("before"), controls(root.Get(), automation.Get()));
        if (args.size() == 4) {
            invoke(root.Get(), automation.Get(), args[3]);
            QThread::msleep(500);
            evidence.insert(QStringLiteral("after"), controls(root.Get(), automation.Get()));
        }
        evidence.insert(QStringLiteral("status"), QStringLiteral("passed"));
        code = 0;
    } catch (const std::exception& error) {
        evidence.insert(QStringLiteral("error"), QString::fromUtf8(error.what()));
    }
    CoUninitialize();
    QTextStream(stdout) << QJsonDocument(evidence).toJson(QJsonDocument::Compact) << Qt::endl;
    return code;
}
