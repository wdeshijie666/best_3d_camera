/**
 * @file CameraParamPanel.cpp
 * @brief 相机参数面板实现：Ela 控件、参数说明 Tooltip
 */
#include "CameraParamPanel.h"
#include "../../controller/DeviceController.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QScrollArea>
#include <QFrame>
#include <QToolTip>

#include <ElaSpinBox.h>
#include <ElaCheckBox.h>
#include <ElaComboBox.h>
#include <ElaScrollArea.h>

namespace scanner_viewer {

CameraParamPanel::CameraParamPanel(QWidget* parent) : QWidget(parent) {
    scroll_area_ = new ElaScrollArea(this);
    scroll_area_->setWidgetResizable(true);
    scroll_area_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll_area_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    form_widget_ = new QWidget(this);
    form_layout_ = new QFormLayout(form_widget_);
    form_layout_->setSpacing(8);
    form_layout_->setContentsMargins(8, 8, 8, 8);
    form_layout_->setLabelAlignment(Qt::AlignLeft);
    scroll_area_->setWidget(form_widget_);
    QVBoxLayout* main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(0, 0, 0, 0);
    main_layout->addWidget(scroll_area_);
}

CameraParamPanel::~CameraParamPanel() = default;

void CameraParamPanel::SetDeviceController(DeviceController* ctrl) {
    device_ctrl_ = ctrl;
    RefreshParameterList();
}

void CameraParamPanel::SetDeveloperMode(bool developer_mode) {
    if (developer_mode_ != developer_mode) {
        developer_mode_ = developer_mode;
        RefreshParameterList();
    }
}

void CameraParamPanel::RefreshParameterList() {
    param_widgets_.clear();
    while (form_layout_->rowCount() > 0) {
        form_layout_->removeRow(0);
    }
    IDeviceAdapter* adapter = device_ctrl_ ? device_ctrl_->GetAdapter() : nullptr;
    if (!adapter || !adapter->IsConnected()) {
        QLabel* hint = new QLabel(tr(u8"请先连接相机"), form_widget_);
        form_layout_->addRow(hint);
        return;
    }
    param_list_.clear();
    if (!adapter->GetParameterList(param_list_)) return;
    for (const ParamMeta& meta : param_list_) {
        if (meta.developer_only && !developer_mode_) continue;
        QWidget* control = nullptr;
        if (meta.kind == ParamKind::Bool) {
            ElaCheckBox* cb = new ElaCheckBox("", form_widget_);
            cb->setProperty("param_id", meta.id);
            connect(cb, &QAbstractButton::toggled, this, [this](bool checked) { OnParamBoolChanged(checked ? 1 : 0); });
            control = cb;
        } else if (meta.kind == ParamKind::Enum && !meta.enum_labels.empty()) {
            ElaComboBox* combo = new ElaComboBox(form_widget_);
            combo->setProperty("param_id", meta.id);
            for (const auto& label : meta.enum_labels)
                combo->addItem(QString::fromStdString(label));
            connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &CameraParamPanel::OnParamEnumChanged);
            control = combo;
        } else {
            ElaSpinBox* spin = new ElaSpinBox(form_widget_);
            spin->setProperty("param_id", meta.id);
            spin->setRange(meta.min_val, meta.max_val);
            if (!meta.unit.empty())
                spin->setSuffix(QString(" %1").arg(QString::fromStdString(meta.unit)));
            connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this, &CameraParamPanel::OnParamIntChanged);
            control = spin;
        }
        QLabel* name_label = new QLabel(QString::fromStdString(meta.name), form_widget_);
        name_label->setToolTip(QString::fromStdString(meta.description));
        name_label->setCursor(Qt::WhatsThisCursor);
        if (!meta.unit.empty() && meta.kind != ParamKind::Enum)
            name_label->setText(name_label->text() + " (" + QString::fromStdString(meta.unit) + ")");
        form_layout_->addRow(name_label, control);
        ParamWidget pw;
        pw.param_id = meta.id;
        pw.control = control;
        pw.name_label = name_label;
        param_widgets_.push_back(pw);
    }
    LoadCurrentValues();
}

void CameraParamPanel::LoadCurrentValues() {
    IDeviceAdapter* adapter = device_ctrl_ ? device_ctrl_->GetAdapter() : nullptr;
    if (!adapter) return;
    for (const ParamWidget& pw : param_widgets_) {
        std::vector<int> values;
        if (!adapter->GetParameter(pw.param_id, values) || values.empty()) continue;
        QSignalBlocker blocker(pw.control);
        if (ElaSpinBox* spin = qobject_cast<ElaSpinBox*>(pw.control))
            spin->setValue(values[0]);
        else if (ElaCheckBox* cb = qobject_cast<ElaCheckBox*>(pw.control))
            cb->setChecked(values[0] != 0);
        else if (ElaComboBox* combo = qobject_cast<ElaComboBox*>(pw.control)) {
            int idx = 0;
            const ParamMeta* meta = nullptr;
            for (const auto& m : param_list_) if (m.id == pw.param_id) { meta = &m; break; }
            if (meta && !meta->enum_values.empty()) {
                for (size_t i = 0; i < meta->enum_values.size(); i++)
                    if (meta->enum_values[i] == values[0]) { idx = static_cast<int>(i); break; }
            } else
                idx = values[0] >= 0 && values[0] < combo->count() ? values[0] : 0;
            combo->setCurrentIndex(idx);
        }
    }
}

void CameraParamPanel::OnParamIntChanged(int value) {
    QObject* s = sender();
    if (!s || !device_ctrl_) return;
    int id = s->property("param_id").toInt();
    IDeviceAdapter* adapter = device_ctrl_->GetAdapter();
    if (adapter)
        adapter->SetParameter(id, {value});
}

void CameraParamPanel::OnParamBoolChanged(int checked) {
    QObject* s = sender();
    if (!s || !device_ctrl_) return;
    int id = s->property("param_id").toInt();
    IDeviceAdapter* adapter = device_ctrl_->GetAdapter();
    if (adapter)
        adapter->SetParameter(id, {checked});
}

void CameraParamPanel::OnParamEnumChanged(int index) {
    QObject* s = sender();
    if (!s || !device_ctrl_) return;
    int id = s->property("param_id").toInt();
    int value = index;
    for (const auto& m : param_list_)
        if (m.id == id && !m.enum_values.empty() && index >= 0 && index < static_cast<int>(m.enum_values.size())) {
            value = m.enum_values[static_cast<size_t>(index)];
            break;
        }
    IDeviceAdapter* adapter = device_ctrl_->GetAdapter();
    if (adapter)
        adapter->SetParameter(id, {value});
}

}  // namespace scanner_viewer
