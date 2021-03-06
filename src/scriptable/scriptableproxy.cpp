/*
    Copyright (c) 2014, Lukas Holecek <hluk@email.cz>

    This file is part of CopyQ.

    CopyQ is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    CopyQ is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with CopyQ.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "scriptable.h"

#include "common/commandstatus.h"
#include "common/common.h"
#include "common/contenttype.h"
#include "common/log.h"
#include "common/mimetypes.h"
#include "common/settings.h"
#include "gui/configurationmanager.h"
#include "gui/mainwindow.h"
#include "item/serialize.h"
#include "platform/platformnativeinterface.h"

#include <QDialog>
#include <QHBoxLayout>
#include <QVBoxLayout>

#include <QCheckBox>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPushButton>
#include <QSpinBox>
#include <QTextEdit>

#define BROWSER(call) \
    ClipboardBrowser *c = fetchBrowser(); \
    if (c) \
        c->call

#define BROWSER_RESULT(call) \
    ClipboardBrowser *c = fetchBrowser(); \
    v = c ? QVariant(c->call) : QVariant()

Q_DECLARE_METATYPE(QFile*)

namespace {

const char propertyWidgetName[] = "CopyQ_widget_name";
const char propertyWidgetProperty[] = "CopyQ_widget_property";

#ifndef HAS_TESTS
QString noTestsError()
{
    return QString("This is only available if tests are compiled!");
}
#endif

QByteArray serializeWindow(WId winId)
{
    QByteArray data;
    return createPlatformNativeInterface()->serialize(winId, &data) ? data : QByteArray();
}

QWidget *label(Qt::Orientation orientation, const QString &name, QWidget *w)
{
    QWidget *parent = w->parentWidget();

    if ( !name.isEmpty() ) {
        QBoxLayout *layout;
        if (orientation == Qt::Horizontal)
            layout = new QHBoxLayout;
        else
            layout = new QVBoxLayout;

        parent->layout()->addItem(layout);

        QLabel *label = new QLabel(name + ":", parent);
        label->setBuddy(w);
        layout->addWidget(label);
        layout->addWidget(w, 1);
    }

    w->setProperty(propertyWidgetName, name);

    return w;
}

QWidget *label(const QString &name, QWidget *w)
{
    w->setProperty("text", name);
    w->setProperty(propertyWidgetName, name);
    return w;
}

template <typename Widget>
Widget *createAndSetWidget(const char *propertyName, const QVariant &value, QWidget *parent)
{
    Widget *w = new Widget(parent);
    w->setProperty(propertyName, value);
    w->setProperty(propertyWidgetProperty, propertyName);
    parent->layout()->addWidget(w);
    return w;
}

QWidget *createDateTimeEdit(
        const QString &name, const char *propertyName, const QVariant &value, QWidget *parent)
{
    QDateTimeEdit *w = createAndSetWidget<QDateTimeEdit>(propertyName, value, parent);
    w->setCalendarPopup(true);
    return label(Qt::Horizontal, name, w);
}

QWidget *createListWidget(const QString &name, const QStringList &items, QWidget *parent)
{
    QComboBox *w = createAndSetWidget<QComboBox>("currentText", QVariant(), parent);
    w->setEditable(true);
    w->addItems(items.mid(1));
    w->setCurrentIndex(-1);
    w->lineEdit()->setText(items.value(0));
    w->lineEdit()->selectAll();
    return label(Qt::Horizontal, name, w);
}

QWidget *createSpinBox(const QString &name, const QVariant &value, QWidget *parent)
{
    QSpinBox *w = createAndSetWidget<QSpinBox>("value", value, parent);
    w->setRange(-1e9, 1e9);
    return label(Qt::Horizontal, name, w);
}

QWidget *createFileNameEdit(const QString &name, const QFile &file, QWidget *parent)
{
    QWidget *w = new QWidget(parent);
    parent->layout()->addWidget(w);

    QHBoxLayout *layout = new QHBoxLayout(w);
    layout->setContentsMargins(0, 0, 0, 0);

    QLineEdit *lineEdit = createAndSetWidget<QLineEdit>("text", file.fileName(), w);
    lineEdit->setProperty(propertyWidgetName, name);

    QPushButton *browseButton = new QPushButton("...");

    QFileDialog *dialog = new QFileDialog(w, name, file.fileName());
    QObject::connect( browseButton, SIGNAL(clicked()),
                      dialog, SLOT(exec()) );
    QObject::connect( dialog, SIGNAL(fileSelected(QString)),
                      lineEdit, SLOT(setText(QString)) );

    layout->addWidget(lineEdit);
    layout->addWidget(browseButton);

    label(Qt::Vertical, name, w);

    return lineEdit;
}

QWidget *createTextEdit(const QString &name, const QVariant &value, QWidget *parent)
{
    QTextEdit *w = createAndSetWidget<QTextEdit>("plainText", value, parent);
    w->setTabChangesFocus(true);
    return label(Qt::Vertical, name, w);
}

QWidget *createWidget(const QString &name, const QVariant &value, QWidget *parent)
{
    switch ( value.type() ) {
    case QVariant::Bool:
        return label(name, createAndSetWidget<QCheckBox>("checked", value, parent));
    case QVariant::Int:
        return createSpinBox("value", value, parent);
    case QVariant::Date:
        return createDateTimeEdit(name, "date", value, parent);
    case QVariant::Time:
        return createDateTimeEdit(name, "time", value, parent);
    case QVariant::DateTime:
        return createDateTimeEdit(name, "dateTime", value, parent);
    case QVariant::List:
        return createListWidget(name, value.toStringList(), parent);
    default:
        QFile *file = value.value<QFile*>();
        if (file) {
            return createFileNameEdit(name, *file, parent);
        } else {
            const QString text = value.toString();
            if (text.contains('\n'))
                return createTextEdit(name, value.toStringList(), parent);
        }

        return label(Qt::Horizontal, name, createAndSetWidget<QLineEdit>("text", value, parent));
    }
}

QVariant config(const QString &name, const QString &value)
{
    ConfigurationManager *cm = ConfigurationManager::instance();

    if ( name.isNull() ) {
        // print options
        QStringList options = cm->options();
        options.sort();
        QString opts;
        foreach (const QString &option, options)
            opts.append( option + "\n  " + cm->optionToolTip(option).replace('\n', "\n  ") + '\n' );
        return opts;
    }

    if ( cm->options().contains(name) ) {
        if ( value.isNull() )
            return cm->value(name).toString(); // return option value

        // set option
        cm->setValue(name, value);

        return QString();
    }

    return QVariant();
}

} // namespace

namespace detail {

ScriptableProxyHelper::ScriptableProxyHelper(MainWindow *mainWindow, const QVariantMap &actionData)
    : QObject(NULL)
    , m_wnd(mainWindow)
    , m_tabName()
    , m_lock()
    , m_actionData(actionData)
{
    qRegisterMetaType< QPointer<QWidget> >("QPointer<QWidget>");
    moveToThread(m_wnd->thread());
}

const QVariant &ScriptableProxyHelper::value() const
{
    return v;
}

QString ScriptableProxyHelper::tabNotFoundError()
{
    return ScriptableProxyHelper::tr("Tab with given name doesn't exist!");
}

QString ScriptableProxyHelper::tabNameEmptyError()
{
    return ScriptableProxyHelper::tr("Tab name cannot be empty!");
}

void ScriptableProxyHelper::close()
{
    m_wnd->close();
}

void ScriptableProxyHelper::showWindow()
{
    m_wnd->showWindow();
}

void ScriptableProxyHelper::pasteToCurrentWindow()
{
    PlatformWindowPtr window = createPlatformNativeInterface()->getCurrentWindow();
    v = !window.isNull();
    if (window)
        window->pasteClipboard();
}

void ScriptableProxyHelper::copyFromCurrentWindow()
{
    PlatformWindowPtr window = createPlatformNativeInterface()->getCurrentWindow();
    v = !window.isNull();
    if (window)
        window->copy();
}

void ScriptableProxyHelper::abortAutomaticCommands()
{
    m_wnd->abortAutomaticCommands();
}

void ScriptableProxyHelper::isMonitoringEnabled()
{
    v = m_wnd->isMonitoringEnabled();
}

void ScriptableProxyHelper::disableMonitoring(bool arg1)
{
    m_wnd->disableClipboardStoring(arg1);
}

void ScriptableProxyHelper::setClipboard(const QVariantMap &data, QClipboard::Mode mode)
{
    m_wnd->setClipboard(data, mode);
}

void ScriptableProxyHelper::renameTab(const QString &arg1, const QString &arg2)
{
    v = QString();

    if ( arg1.isEmpty() || arg2.isEmpty() ) {
        v = tabNameEmptyError();
        return;
    }

    const int i = m_wnd->findTabIndex(arg2);
    if (i == -1) {
        v = tabNotFoundError();
        return;
    }

    if ( m_wnd->findTabIndex(arg1) != -1 ) {
        v = tr("Tab with given name already exists!");
        return;
    }

    m_wnd->renameTab(arg1, i);
}

void ScriptableProxyHelper::removeTab(const QString &arg1)
{
    v = QString();

    if ( arg1.isEmpty() ) {
        v = tabNameEmptyError();
        return;
    }

    const int i = m_wnd->findTabIndex(arg1);
    if (i == -1) {
        v = tabNotFoundError();
        return;
    }

    m_wnd->removeTab(false, i);
}

void ScriptableProxyHelper::tabIcon(const QString &tabName)
{
    v = ConfigurationManager::instance()->getIconNameForTabName(tabName);
}

void ScriptableProxyHelper::setTabIcon(const QString &tabName, const QString &icon)
{
    m_wnd->setTabIcon(tabName, icon);
}

void ScriptableProxyHelper::showBrowser(const QString &tabName)
{
    ClipboardBrowser *c = fetchBrowser(tabName);
    if (c)
        m_wnd->showBrowser(c);
}

void ScriptableProxyHelper::showBrowser()
{
    showBrowser(m_tabName);
}

void ScriptableProxyHelper::action(const QVariantMap &arg1, const Command &arg2)
{
    m_wnd->action(arg1, arg2);
}

void ScriptableProxyHelper::showMessage(const QString &arg1, const QString &arg2, QSystemTrayIcon::MessageIcon arg3, int arg4)
{
    m_wnd->showMessage(arg1, arg2, arg3, arg4);
}

void ScriptableProxyHelper::browserLock()
{
    Q_ASSERT(m_lock.isNull());
    ClipboardBrowser *c = fetchBrowser();
    if (c)
        m_lock.reset( new ClipboardBrowser::Lock(c) );
}

void ScriptableProxyHelper::browserUnlock()
{
    Q_ASSERT(!m_lock.isNull());
    m_lock.reset();
}

void ScriptableProxyHelper::nextToClipboard(int where)
{
    ClipboardBrowser *c = fetchBrowser();

    const int row = qMax(0, c->currentIndex().row()) + where;
    const QModelIndex index = c->index(row);

    if (!index.isValid())
        return;

    setClipboard(::itemData(index), QClipboard::Clipboard);
    c->setCurrentIndex(index);
}

void ScriptableProxyHelper::browserMoveToClipboard(int arg1)
{
    ClipboardBrowser *c = fetchBrowser();
    if (c)
        c->moveToClipboard(c->index(arg1));
}

void ScriptableProxyHelper::browserSetCurrent(int arg1)
{
    BROWSER(setCurrent(arg1));
}

void ScriptableProxyHelper::browserRemoveRows(QList<int> rows)
{
    ClipboardBrowser *c = fetchBrowser();
    if (!c)
        return;

    qSort( rows.begin(), rows.end(), qGreater<int>() );

    ClipboardBrowser::Lock lock(c);
    foreach (int row, rows)
        c->removeRow(row);
}

void ScriptableProxyHelper::browserEditRow(int arg1)
{
    BROWSER(editRow(arg1));
}

void ScriptableProxyHelper::browserEditNew(const QString &arg1, bool changeClipboard)
{
    BROWSER(editNew(arg1, changeClipboard));
}

void ScriptableProxyHelper::tabs()
{
    v = m_wnd->tabs();
}

void ScriptableProxyHelper::toggleVisible()
{
    v = m_wnd->toggleVisible();
}

void ScriptableProxyHelper::toggleMenu(const QString &tabName)
{
    v = m_wnd->toggleMenu(fetchBrowser(tabName));
}

void ScriptableProxyHelper::toggleMenu()
{
    v = m_wnd->toggleMenu();
}

void ScriptableProxyHelper::mainWinId()
{
    v = serializeWindow(m_wnd->winId());
}

void ScriptableProxyHelper::trayMenuWinId()
{
    v = serializeWindow(m_wnd->trayMenu()->winId());
}

void ScriptableProxyHelper::findTabIndex(const QString &arg1)
{
    v = m_wnd->findTabIndex(arg1);
}

void ScriptableProxyHelper::openActionDialog(const QVariantMap &arg1)
{
    v = (qulonglong)m_wnd->openActionDialog(arg1);
}

void ScriptableProxyHelper::loadTab(const QString &arg1)
{
    v = m_wnd->loadTab(arg1);
}

void ScriptableProxyHelper::saveTab(const QString &arg1)
{
    v = QString();
    ClipboardBrowser *c = fetchBrowser();
    if (c) {
        const int i = m_wnd->findTabIndex( c->tabName() );
        v = m_wnd->saveTab(arg1, i);
    }
}

void ScriptableProxyHelper::config(const QString &arg1, const QString &arg2)
{
    v = ::config(arg1, arg2);
}

void ScriptableProxyHelper::getClipboardData(const QString &mime, QClipboard::Mode mode)
{
    const QMimeData *data = clipboardData(mode);
    if (!data)
        v = QByteArray();
    else if (mime == "?")
        v = data->formats().join("\n").toUtf8() + '\n';
    else
        v = cloneData(*data, QStringList(mime)).value(mime).toByteArray();
}

void ScriptableProxyHelper::browserLength()
{
    BROWSER_RESULT(length());
}

void ScriptableProxyHelper::browserOpenEditor(const QByteArray &arg1, bool changeClipboard)
{
    BROWSER_RESULT(openEditor(arg1, changeClipboard));
}

void ScriptableProxyHelper::browserAdd(const QString &arg1)
{
    BROWSER_RESULT(add(arg1));
}

void ScriptableProxyHelper::browserAdd(const QStringList &texts) {
    ClipboardBrowser *c = fetchBrowser();
    if (!c) {
        v = false;
        return;
    }

    v = true;

    ClipboardBrowser::Lock lock(c);
    foreach (const QString &text, texts) {
        if ( !c->add(text) ) {
            v = false;
            return;
        }
    }

    return;
}

void ScriptableProxyHelper::browserAdd(const QVariantMap &arg1, int arg2)
{
    BROWSER_RESULT(add(arg1, arg2));
}

void ScriptableProxyHelper::browserChange(const QVariantMap &data, int row)
{
    ClipboardBrowser *c = fetchBrowser();
    if (!c)
        return;

    const QModelIndex index = c->index(row);
    QVariantMap itemData = index.data(contentType::data).toMap();
    foreach (const QString &mime, data.keys())
        itemData[mime] = data[mime];

    c->model()->setData(index, itemData, contentType::data);
}

void ScriptableProxyHelper::browserItemData(int arg1, const QString &arg2)
{
    v = itemData(arg1, arg2);
}

void ScriptableProxyHelper::browserItemData(int arg1)
{
    v = itemData(arg1);
}

void ScriptableProxyHelper::setCurrentTab(const QString &tabName)
{
    m_tabName = tabName;
}

void ScriptableProxyHelper::currentTab()
{
    BROWSER_RESULT(tabName());
}

void ScriptableProxyHelper::currentItem()
{
    v = QVariant();

    if (!canUseSelectedItems())
        return;

    const QPersistentModelIndex current =
            m_actionData.value(mimeCurrentItem).value<QPersistentModelIndex>();
    if (current.isValid())
        v = QVariant::fromValue(current.row());
}

void ScriptableProxyHelper::selectItems(const QList<int> &items)
{
    v = false;

    ClipboardBrowser *c = fetchBrowser();
    if (!c)
        return;

    v = true;

    c->clearSelection();

    if ( !items.isEmpty() ) {
        c->setCurrent(items.last());

        foreach (int i, items) {
            const QModelIndex index = c->index(i);
            if (index.isValid())
                c->selectionModel()->select(index, QItemSelectionModel::Select);
        }
    }
}

void ScriptableProxyHelper::selectedItems()
{
    v = QVariant();

    if (!canUseSelectedItems())
        return;

    QList<int> selectedRows;
    const QList<QPersistentModelIndex> selected =
            m_actionData.value(mimeSelectedItems)
            .value< QList<QPersistentModelIndex> >();
    foreach (const QPersistentModelIndex &index, selected) {
        if (index.isValid())
            selectedRows.append(index.row());
    }

    v = QVariant::fromValue(selectedRows);
}

#ifdef HAS_TESTS
void ScriptableProxyHelper::sendKeys(const QString &keys)
{
    v = QString();

    if (keys == "FLUSH_KEYS")
        return;

    QWidget *w = m_wnd->trayMenu();

    if ( !w->isVisible() ) {
        w = QApplication::focusWidget();
        if (!w) {
            COPYQ_LOG("No focused widget -> using main window");
            w = m_wnd;
        }
    }

    if (keys.startsWith(":")) {
        const QString widgetName = QString("%1 in %2")
                .arg(w->metaObject()->className())
                .arg(w->window()->metaObject()->className());

        COPYQ_LOG( QString("Sending keys \"%1\" to \"%2\".")
                   .arg(keys.mid(1))
                   .arg(widgetName) );

        QTest::keyClicks(w, keys.mid(1), Qt::NoModifier, 50);
    } else {
        const QKeySequence shortcut(keys, QKeySequence::PortableText);

        if ( shortcut.isEmpty() ) {
            v = QString("Cannot parse key \"%1\"!").arg(keys);
            return;
        }

        // Don't stop when modal window is open.
        QMetaObject::invokeMethod( this, "keyClick", Qt::QueuedConnection,
                                   Q_ARG(const QKeySequence &, shortcut),
                                   Q_ARG(const QPointer<QWidget> &, w)
                                   );
    }
}

void ScriptableProxyHelper::testcurrentItem()
{
    BROWSER_RESULT(currentIndex().row());
}

void ScriptableProxyHelper::testselectedTab()
{
    v = m_wnd->browser()->tabName();
}

void ScriptableProxyHelper::testselectedItems()
{
    QModelIndexList selectedRows = m_wnd->browser()->selectionModel()->selectedRows();

    QList<int> result;
    result.reserve( selectedRows.size() );

    foreach (const QModelIndex &index, selectedRows)
        result.append(index.row());

    v = QVariant::fromValue(result);
}

void ScriptableProxyHelper::keyClick(const QKeySequence &shortcut, const QPointer<QWidget> &widget)
{
    const QString keys = shortcut.toString();

    if (widget.isNull()) {
        COPYQ_LOG( QString("Failed to send key \"%1\".").arg(keys) );
        return;
    }

    const QString widgetName = QString("%1 in %2")
            .arg(widget->metaObject()->className())
            .arg(widget->window()->metaObject()->className());

    m_wnd->showMessage( widgetName, shortcut.toString(),
                        QSystemTrayIcon::Information, 4000 );

    COPYQ_LOG( QString("Sending key \"%1\" to \"%2\".")
               .arg(keys)
               .arg(widgetName) );

    QTest::keyClick( widget.data(),
                     Qt::Key(shortcut[0] & ~Qt::KeyboardModifierMask),
            Qt::KeyboardModifiers(shortcut[0] & Qt::KeyboardModifierMask), 0 );

    COPYQ_LOG( QString("Key \"%1\" sent to \"%2\".")
               .arg(keys)
               .arg(widgetName) );
}
#else // HAS_TESTS
void ScriptableProxyHelper::sendKeys(const QString &)
{
    v = noTestsError();
}

void ScriptableProxyHelper::testcurrentItem()
{
    v = noTestsError();
}

void ScriptableProxyHelper::testselectedTab()
{
    v = noTestsError();
}

void ScriptableProxyHelper::testselectedItems()
{
    v = noTestsError();
}

void ScriptableProxyHelper::keyClick(const QKeySequence &, const QPointer<QWidget> &)
{
}
#endif // HAS_TESTS

void ScriptableProxyHelper::currentWindowTitle()
{
    PlatformWindowPtr window = createPlatformNativeInterface()->getCurrentWindow();
    v = window ? window->getTitle() : QString();
}

void ScriptableProxyHelper::inputDialog(const NamedValueList &values)
{
    v.setValue(NamedValueList());

    QDialog dialog(m_wnd);
    QVBoxLayout layout(&dialog);
    QWidgetList widgets;
    widgets.reserve(values.size());

    QString styleSheet;
    QRect geometry(-1, -1, 0, 0);

    foreach (const NamedValue &value, values) {
        if (value.name == ".title")
            dialog.setWindowTitle( value.value.toString() );
        else if (value.name == ".icon")
            dialog.setWindowIcon( QIcon(value.value.toString()) );
        else if (value.name == ".style")
            styleSheet = value.value.toString();
        else if (value.name == ".height")
            geometry.setHeight( pointsToPixels(value.value.toInt()) );
        else if (value.name == ".width")
            geometry.setWidth( pointsToPixels(value.value.toInt()) );
        else if (value.name == ".x")
            geometry.setX(value.value.toInt());
        else if (value.name == ".y")
            geometry.setY(value.value.toInt());
        else if (value.name == ".label")
            createAndSetWidget<QLabel>("text", value.value, &dialog);
        else
            widgets.append( createWidget(value.name, value.value, &dialog) );
    }

    dialog.adjustSize();

    if (geometry.height() <= 0)
        geometry.setHeight(dialog.height());
    if (geometry.width() <= 0)
        geometry.setWidth(dialog.width());
    if (geometry.isValid())
        dialog.resize(geometry.size());
    if (geometry.x() >= 0 && geometry.y() >= 0)
        dialog.move(geometry.topLeft());

    if ( !styleSheet.isEmpty() )
        dialog.setStyleSheet(styleSheet);

    QDialogButtonBox buttons(
                QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dialog);
    connect( &buttons, SIGNAL(accepted()), &dialog, SLOT(accept()) );
    connect( &buttons, SIGNAL(rejected()), &dialog, SLOT(reject()) );
    layout.addWidget(&buttons);

    dialog.show();
    dialog.raise();
    dialog.activateWindow();

    const QByteArray wid = serializeWindow( dialog.winId() );
    if ( !wid.isEmpty() )
        emit sendMessage(wid, CommandActivateWindow);

    if ( dialog.exec() ) {
        NamedValueList result;

        foreach ( QWidget *w, widgets ) {
            const QString propertyName = w->property(propertyWidgetProperty).toString();
            const QString name = w->property(propertyWidgetName).toString();
            const QVariant value = w->property(propertyName.toUtf8().constData());
            result.append( NamedValue(name, value) );
        }

        v.setValue(result);
    }
}

void ScriptableProxyHelper::setUserValue(const QString &key, const QVariant &value)
{
    Settings settings;
    settings.beginGroup("script");
    settings.setValue(key, value);
}

void ScriptableProxyHelper::updateFirstItem(const QVariantMap &data)
{
    m_wnd->updateFirstItem(data);
}

void ScriptableProxyHelper::updateTitle(const QVariantMap &data)
{
    m_wnd->updateTitle(data);
}

} // namespace detail

ClipboardBrowser *detail::ScriptableProxyHelper::fetchBrowser(const QString &tabName)
{
    if (tabName.isEmpty()) {
        const QString defaultTabName = m_actionData.value(mimeCurrentTab).toString();
        if (!defaultTabName.isEmpty())
            return fetchBrowser(defaultTabName);
    }

    ClipboardBrowser *c = tabName.isEmpty() ? m_wnd->browser(0) : m_wnd->createTab(tabName);
    if (!c)
        return NULL;

    c->loadItems();
    return c->isLoaded() ? c : NULL;
}

ClipboardBrowser *detail::ScriptableProxyHelper::fetchBrowser() { return fetchBrowser(m_tabName); }

QVariantMap detail::ScriptableProxyHelper::itemData(int i)
{
    ClipboardBrowser *c = fetchBrowser();
    return c ? ::itemData(c->index(i)) : QVariantMap();
}

QByteArray detail::ScriptableProxyHelper::itemData(int i, const QString &mime)
{
    const QVariantMap data = itemData(i);
    if ( data.isEmpty() )
        return QByteArray();

    if (mime == "?")
        return QStringList(data.keys()).join("\n").toUtf8() + '\n';

    if (mime == mimeItems)
        return serializeData(data);

    return data.value(mime).toByteArray();
}

bool detail::ScriptableProxyHelper::canUseSelectedItems() const
{
    return m_tabName.isEmpty()
            || m_tabName == m_actionData.value(mimeCurrentTab).toString();
}

ScriptableProxy::ScriptableProxy(MainWindow *mainWindow, const QVariantMap &actionData)
    : m_helper(new detail::ScriptableProxyHelper(mainWindow, actionData))
{
    qRegisterMetaType<QSystemTrayIcon::MessageIcon>("SystemTrayIcon::MessageIcon");
}

ScriptableProxy::~ScriptableProxy() { delete m_helper; }
