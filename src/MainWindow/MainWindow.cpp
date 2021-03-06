/**
 * This file is a part of Luminance HDR package.
 * ----------------------------------------------------------------------
 * Copyright (C) 2006-2007 Giuseppe Rota
 * Copyright (C) 2011-2013 Davide Anastasia
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 * ----------------------------------------------------------------------
 *
 * @author Giuseppe Rota <grota@users.sourceforge.net>
 * Original Work
 *
 * @author Franco Comida <fcomida@users.sourceforge.net>
 * Improvements, bugfixing
 *
 * @author Davide Anastasia <davideanastasia@users.sourceforge.net>
 * Implementation of the SDI functionalities
 * New Central Widget based on QTabWidget
 * Division of the Central Widget using QSplitter
 *
 */

#include "MainWindow/MainWindow.h"

#ifdef QT_DEBUG
#include <QDebug>
#endif

#include <QFileDialog>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QMimeData>
#include <QWhatsThis>
#include <QSignalMapper>
#include <QTextStream>
#include <QDesktopServices>
#include <QTimer>
#include <QString>
#include <QtConcurrentRun>

#include <boost/bind.hpp>

#include "ui_MainWindow.h"

#include "MainWindow/DnDOption.h"
#include "MainWindow/UpdateChecker.h"
#include "MainWindow/DonationDialog.h"
#include "MainWindow/ExportToHtmlDialog.h"
#include "MainWindow/SupportedCamerasDialog.h"

#include "Libpfs/frame.h"
#include "Libpfs/params.h"
#include "Libpfs/manip/cut.h"
#include "Libpfs/manip/copy.h"
#include "Libpfs/manip/rotate.h"
#include "Libpfs/manip/gamma_levels.h"
#include "Fileformat/pfsoutldrimage.h"

#include "Common/archs.h"
#include "Common/config.h"
#include "Common/global.h"
#include "Common/CommonFunctions.h"
#include "OsIntegration/osintegration.h"
#include "BatchHDR/BatchHDRDialog.h"
#include "BatchTM/BatchTMDialog.h"

#include "TransplantExif/TransplantExifDialog.h"
#include "Viewers/HdrViewer.h"
#include "Viewers/LuminanceRangeWidget.h"
#include "Viewers/LdrViewer.h"
#include "UI/ImageQualityDialog.h"
#include "UI/TiffModeDialog.h"
#include "UI/UMessageBox.h"
#include "UI/GammaAndLevels.h"

#ifdef HAVE_CFITSIO
#include "UI/FitsImporter.h"
#endif

#include "PreviewPanel/PreviewPanel.h"
#include "HelpBrowser/helpbrowser.h"
#include "TonemappingPanel/TMOProgressIndicator.h"
#include "TonemappingPanel/TonemappingPanel.h"
#include "HdrWizard/HdrWizard.h"
#include "Resize/ResizeDialog.h"
#include "Projection/ProjectionsDialog.h"
#include "Preferences/PreferencesDialog.h"
#include "Core/IOWorker.h"
#include "Core/TMWorker.h"
#include "TonemappingPanel/TMOProgressIndicator.h"
#include "HdrWizard/AutoAntighosting.h"
#include "HdrWizard/WhiteBalance.h"
#include "LibpfsAdditions/formathelper.h"

namespace
{
QString getLdrFileNameFromSaveDialog(const QString& suggestedFileName, QWidget* parent = 0)
{
    QString filetypes = QObject::tr("All LDR formats");
    filetypes += " (*.jpg *.jpeg *.png *.ppm *.pbm *.bmp *.JPG *.JPEG *.PNG *.PPM *.PBM *.BMP);;";
    filetypes += "JPEG (*.jpg *.jpeg *.JPG *.JPEG);;" ;
    filetypes += "PNG (*.png *.PNG);;";
    filetypes += "PPM PBM (*.ppm *.pbm *.PPM *.PBM);;";
    filetypes += "BMP (*.bmp *.BMP);;";
    filetypes += "TIFF (*.tif *.tiff *.TIF *.TIFF)";

    QFileInfo qfi(suggestedFileName);
    QString outputFilename =
            QFileDialog::getSaveFileName(parent,
                                         QObject::tr("Save the LDR image as..."),
                                         LuminanceOptions().getDefaultPathLdrOut() + QDir::separator() + qfi.completeBaseName(),
                                         filetypes);

    if ( !outputFilename.isEmpty() )
    {
		QFileInfo qfi(outputFilename);

		LuminanceOptions().setDefaultPathLdrOut( qfi.path() );
    }
    return outputFilename;
}

QString getHdrFileNameFromSaveDialog(const QString& suggestedFileName, QWidget* parent = 0)
{
#ifdef QT_DEBUG
	qDebug() << "MainWindow::getHdrFileNameFromSaveDialog(" << suggestedFileName << ")";
#endif
    static const QString filetypes =
            "OpenEXR (*.exr *.EXR);;"
            "HDR TIFF (*.tiff *.tif *.TIFF *.TIF);;"
            "Radiance RGBE (*.hdr *.pic *.HDR *.PIC);;"
            "PFS Stream (*.pfs *.PFS)";

    QFileInfo qfi(suggestedFileName);

    QString outputFilename =
            QFileDialog::getSaveFileName(parent,
                                         QObject::tr("Save the HDR image as..."),
                                         LuminanceOptions().getDefaultPathHdrOut() + QDir::separator() + qfi.completeBaseName(),
                                         filetypes);

    if ( !outputFilename.isEmpty() )
    {
        QFileInfo qfi(outputFilename);

        LuminanceOptions().setDefaultPathHdrOut( qfi.path() );
    }
    return outputFilename;
}

void getCropCoords(GenericViewer* gv, int& x_ul, int& y_ul, int& x_br, int& y_br)
{
    assert( gv != NULL );

    QRect cropRect = gv->getSelectionRect().normalized();
    cropRect.getCoords(&x_ul, &y_ul, &x_br, &y_br);
}

GenericViewer::ViewerMode getCurrentViewerMode(const QTabWidget& curr_tab_widget)
{
    if (curr_tab_widget.count() <= 0)
    {
        return GenericViewer::FIT_WINDOW;
    }
    else
    {
        GenericViewer* g_v = (GenericViewer*)curr_tab_widget.currentWidget();
        return g_v->getViewerMode();
    }
}


}

// static members!
int MainWindow::sm_NumMainWindows = 0;
QScopedPointer<UpdateChecker> MainWindow::sm_updateChecker;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_Ui(new Ui::MainWindow)
    , m_exportQueueSize(0)
    , m_fullScreenLdrViewer(new LdrViewer(NULL))
    , m_fullScreenHdrViewer(new HdrViewer(NULL))
    , m_firstWindow(0)
{
    init();
}

MainWindow::MainWindow(pfs::Frame* curr_frame, const QString& new_file,
                       const QStringList& inputFileNames,
                       bool needSaving, QWidget *parent)
    : QMainWindow(parent)
    , m_Ui(new Ui::MainWindow)
    , m_exportQueueSize(0)
    , m_fullScreenLdrViewer(new LdrViewer(NULL))
    , m_fullScreenHdrViewer(new HdrViewer(NULL))
    , m_firstWindow(0)
{
    init();

    emit load_success(curr_frame, new_file, inputFileNames, needSaving);
}

MainWindow::~MainWindow()
{
    sm_NumMainWindows--;

#ifdef QT_DEBUG
    qDebug() << "MainWindow::~MainWindow() = " << sm_NumMainWindows;
#endif

    if ( sm_NumMainWindows == 0 )
    {
        // Last MainWindow is dead...
        luminance_options->setValue("MainWindowState", saveState());
        luminance_options->setValue("MainWindowGeometry", saveGeometry());
        luminance_options->setValue("MainWindowSplitterState", m_centralwidget_splitter->saveState());
        luminance_options->setValue("MainWindowSplitterGeometry", m_centralwidget_splitter->saveGeometry());
        luminance_options->setValue("MainWindowBottomSplitterState", m_bottom_splitter->saveState());
        luminance_options->setValue("MainWindowBottomSplitterGeometry", m_bottom_splitter->saveGeometry());

        // wait for the working thread to finish
        m_IOThread->wait(500);
        m_TMThread->wait(500);
		//m_QueueThread->wait(500);

        sm_updateChecker.reset();
    }

    clearRecentFileActions();
    delete luminance_options;
    delete m_fullScreenLdrViewer;
    delete m_fullScreenHdrViewer;
}

void MainWindow::init()
{
    luminance_options = new LuminanceOptions();

    sm_NumMainWindows++;
    m_firstWindow = 0;

    helpBrowser = NULL;
    num_ldr_generated = 0;
    curr_num_ldr_open = 0;
    splash = 0;
    m_processingAWB = false;

    if ( sm_NumMainWindows == 1 )
    {
        // Register symbols on the first activation!
        qRegisterMetaType<QImage>("QImage");
        qRegisterMetaType<pfs::Frame*>("pfs::Frame*");
        qRegisterMetaType<TonemappingOptions>("TonemappingOptions");
        qRegisterMetaType<TonemappingOptions*>("TonemappingOptions*");
        qRegisterMetaType<HdrViewer*>("HdrViewer*");
        qRegisterMetaType<LdrViewer*>("LdrViewer*");
        qRegisterMetaType<GenericViewer*>("GenericViewer*");
        qRegisterMetaType<QVector<float> >("QVector<float>");
        qRegisterMetaType<pfs::Params>("pfs::Params");
        qRegisterMetaType<pfs::Params*>("pfs::Params*");
    }

    createUI();
    loadOptions();
    createMenus();
    createToolBar();
    createCentralWidget();
    createStatusBar();
    setupIO();
    setupTM();
	setupQueue();
    createConnections();

    OsIntegration::getInstance().init(this);

    if ( sm_NumMainWindows == 1 ) {
        if (OsIntegration::getInstance().isRunningOnSameCpuPlatform()) 
        {
			sm_updateChecker.reset(new UpdateChecker(this));
			connect(sm_updateChecker.data(), SIGNAL(updateAvailable()), this, SLOT(onUpdateAvailable()));

            m_firstWindow = 2;
        }
		else
            m_firstWindow = 1;
    }
    
}

void MainWindow::createUI()
{
    m_Ui->setupUi(this);
    setAttribute(Qt::WA_DeleteOnClose);

    restoreState(luminance_options->value("MainWindowState").toByteArray());
    restoreGeometry(luminance_options->value("MainWindowGeometry").toByteArray());

    if(isFullScreen()) m_Ui->actionShow_Full_Screen->setChecked(true);

    setAcceptDrops(true);
    setWindowModified(false);
    setWindowTitle(QString("Luminance HDR " LUMINANCEVERSION)); // + "  " + g_GIT_SHA1);
}

void MainWindow::createCentralWidget()
{
    // Central Widget Area
    m_centralwidget_splitter = new QSplitter; //(this);
    m_bottom_splitter = new QSplitter; //(this);
    m_bottom_splitter->setOrientation(Qt::Vertical);

    setCentralWidget(m_centralwidget_splitter);

    m_PreviewPanel = new PreviewPanel();

    // create tonemapping panel
    m_tonemapPanel = new TonemappingPanel(sm_NumMainWindows, m_PreviewPanel); //(m_centralwidget_splitter);

    connect(m_Ui->actionRealtimePreviews, SIGNAL(toggled(bool)), m_tonemapPanel, SLOT(setRealtimePreviews(bool)));
    connect(m_Ui->actionRealtimePreviews, SIGNAL(toggled(bool)), luminance_options, SLOT(setRealtimePreviewsActive(bool)));
    m_tonemapPanel->setRealtimePreviews(luminance_options->isRealtimePreviewsActive());

    m_tabwidget = new QTabWidget; //(m_centralwidget_splitter);

    m_tabwidget->setDocumentMode(true);
    m_tabwidget->setTabsClosable(true);

    m_centralwidget_splitter->addWidget(m_tonemapPanel);
    m_centralwidget_splitter->addWidget(m_bottom_splitter);

    m_bottom_splitter->addWidget(m_tabwidget);

    m_bottom_splitter->setStretchFactor(0, 10);
    m_bottom_splitter->setStretchFactor(1, 1);
    m_centralwidget_splitter->setStretchFactor(0, 1);
    m_centralwidget_splitter->setStretchFactor(1, 5);

    // create preview panel
    m_PreviewscrollArea = new QScrollArea;
    m_PreviewscrollArea->setWidgetResizable(true);
    m_PreviewscrollArea->setWidget(m_PreviewPanel);

    m_centralwidget_splitter->addWidget(m_PreviewscrollArea);
    m_centralwidget_splitter->setCollapsible(2, false);

    if (luminance_options->getPreviewPanelMode()) {
        showPreviewsOnTheBottom();
    }
    else {
        showPreviewsOnTheRight();
    }

    connect(m_tabwidget, SIGNAL(tabCloseRequested(int)), this, SLOT(removeTab(int)));
    connect(m_tabwidget, SIGNAL(currentChanged(int)), this, SLOT(updateActions(int)));
    connect(m_tabwidget, SIGNAL(currentChanged(int)), this, SLOT(updateSoftProofing(int)));
    connect(m_tonemapPanel, SIGNAL(startTonemapping(TonemappingOptions*)), this, SLOT(tonemapImage(TonemappingOptions*)));
    connect(m_tonemapPanel, SIGNAL(startExport(TonemappingOptions*)), this, SLOT(exportImage(TonemappingOptions*)));
    connect(this, SIGNAL(updatedHDR(pfs::Frame*)), m_tonemapPanel, SLOT(updatedHDR(pfs::Frame*)));
    connect(this, SIGNAL(destroyed()), m_PreviewPanel, SLOT(deleteLater()));

    m_centralwidget_splitter->restoreState(luminance_options->value("MainWindowSplitterState").toByteArray());
    m_centralwidget_splitter->restoreGeometry(luminance_options->value("MainWindowSplitterGeometry").toByteArray());
    m_bottom_splitter->restoreState(luminance_options->value("MainWindowBottomSplitterState").toByteArray());
    m_bottom_splitter->restoreGeometry(luminance_options->value("MainWindowBottomSplitterGeometry").toByteArray());

	QPalette pal = m_tabwidget->palette();
	pal.setColor(QPalette::Dark, Qt::darkGray);
	m_tabwidget->setPalette(pal);

    m_tabwidget->setBackgroundRole(QPalette::Dark);
    m_tabwidget->setAutoFillBackground( true );

    // replace with ->tabBar() one day, or subclass
    QTabBar* tabBar = m_tabwidget->findChild<QTabBar *>(QLatin1String("qt_tabwidget_tabbar"));
    tabBar->setAutoFillBackground( true );
    tabBar->setBackgroundRole(QPalette::Window);

    m_PreviewscrollArea->hide();
}

void MainWindow::createToolBar()
{
    // main toolbars setup
    QActionGroup *toolBarOptsGroup = new QActionGroup(this);
    toolBarOptsGroup->addAction(m_Ui->actionText_Under_Icons);
    toolBarOptsGroup->addAction(m_Ui->actionIcons_Only);
    toolBarOptsGroup->addAction(m_Ui->actionText_Alongside_Icons);
    toolBarOptsGroup->addAction(m_Ui->actionText_Only);
    m_Ui->menuToolbars->addAction(m_Ui->toolBar->toggleViewAction());

    connect(m_Ui->actionText_Under_Icons,SIGNAL(triggered()),this,SLOT(Text_Under_Icons()));
    connect(m_Ui->actionIcons_Only,SIGNAL(triggered()),this,SLOT(Icons_Only()));
    connect(m_Ui->actionText_Alongside_Icons,SIGNAL(triggered()),this,SLOT(Text_Alongside_Icons()));
    connect(m_Ui->actionText_Only,SIGNAL(triggered()),this,SLOT(Text_Only()));

    // Preview Panel
    QActionGroup *previewPanelOptsGroup = new QActionGroup(this);
    previewPanelOptsGroup->addAction(m_Ui->actionShow_on_the_right);
    previewPanelOptsGroup->addAction(m_Ui->actionShow_on_the_bottom);
    connect(m_Ui->actionShow_on_the_right, SIGNAL(triggered()), this, SLOT(showPreviewsOnTheRight()));
    connect(m_Ui->actionShow_on_the_bottom, SIGNAL(triggered()), this, SLOT(showPreviewsOnTheBottom()));
}

void MainWindow::createMenus()
{
    // icons
    m_Ui->fileNewAction->setIcon(QIcon::fromTheme("insert-image", QIcon(":/new/svgs/images/insert-image.svgz")));
    m_Ui->fileOpenAction->setIcon(QIcon::fromTheme("document-open", QIcon(":/new/svgs/images/document-open.svgz")));
    m_Ui->fileSaveAsAction->setIcon(QIcon::fromTheme("document-save", QIcon(":/new/prefix1/images/filesave.png")));
    m_Ui->fileExitAction->setIcon(QIcon::fromTheme("application-exit", QIcon(":/new/prefix1/images/exit.png")));
    m_Ui->rotateccw->setIcon(QIcon::fromTheme("object-rotate-left", QIcon(":/new/prefix1/images/rotate_left.png")));
    m_Ui->rotatecw->setIcon(QIcon::fromTheme("object-rotate-right", QIcon(":/new/prefix1/images/rotate_right.png")));
    m_Ui->documentationAction->setIcon(QIcon::fromTheme("help-contents", QIcon(":/new/prefix1/images/help.png")));
    m_Ui->normalSizeAct->setIcon(QIcon::fromTheme("zoom-original", QIcon(":/new/svgs/images/zoom-original.svgz")));
    m_Ui->zoomInAct->setIcon(QIcon::fromTheme("zoom-in", QIcon(":/new/svgs/images/zoom-in.svgz")));
    m_Ui->zoomOutAct->setIcon(QIcon::fromTheme("zoom-out", QIcon(":/new/svgs/images/zoom-out.svgz")));
    m_Ui->fitToWindowAct->setIcon(QIcon::fromTheme("zoom", QIcon(":/new/svgs/images/page-zoom.svgz")));
    m_Ui->OptionsAction->setIcon(QIcon::fromTheme("preferences-system", QIcon(":/new/prefix1/images/options.png")));
    //m_Ui->actionResize->setIcon(QIcon::fromTheme("", QIcon(":/new/prefix1/images/scale.png")));
    m_Ui->Transplant_Exif_Data_action->setIcon(QIcon::fromTheme("selection-move-to-layer-below", QIcon(":/new/prefix1/images/copyexif.png")));
    m_Ui->actionBatch_Tone_Mapping->setIcon(QIcon::fromTheme("system-run", QIcon(":/new/prefix1/images/system-run.png"))); // TODO
    m_Ui->actionWhat_s_This->setIcon(QIcon::fromTheme("help-whatsthis", QIcon(":/new/prefix1/images/whatsthis.png")));
    m_Ui->actionAbout_Luminance->setIcon(QIcon::fromTheme("help-about", QIcon(":/new/prefix1/images/help-about.png")));
    m_Ui->actionSave_Hdr_Preview->setIcon(QIcon::fromTheme("document-save-as-template", QIcon(":/new/prefix1/images/preview-file.png")));
    m_Ui->cropToSelectionAction->setIcon(QIcon::fromTheme("transform-crop", QIcon(":/new/prefix1/images/crop.png")));
    m_Ui->removeSelectionAction->setIcon(QIcon::fromTheme("edit-select-none", QIcon(":/new/prefix1/images/tool_rect_selection.png")));
    m_Ui->actionDonate->setIcon(QIcon::fromTheme("help-donate", QIcon(":/new/prefix1/images/help-donate.png")));
    m_Ui->actionLock->setIcon(QIcon::fromTheme("system-lock-screen", QIcon(":/new/prefix1/images/lock-on.png")));
    m_Ui->fileSaveAllAction->setIcon(QIcon::fromTheme("document-save-all", QIcon(":/new/prefix1/images/filesaveall.png")));
    m_Ui->actionFill_to_Window->setIcon(QIcon::fromTheme("zoom-fit-best", QIcon(":/new/svgs/images/zoom-fit-best.svgz")));
    m_Ui->actionWhite_Balance->setIcon(QIcon::fromTheme("whitebalance", QIcon(":/new/svgs/images/color-picker-white.svg")));
    m_Ui->actionMinimize->setIcon(QIcon::fromTheme("window-minimize-symbolic", QIcon(":/new/svgs/images/window-minimize.svg")));
    m_Ui->actionMaximize->setIcon(QIcon::fromTheme("window-maximize-symbolic", QIcon(":/new/svgs/images/window-maximize.svg")));
    m_Ui->actionRemove_Tab->setIcon(QIcon::fromTheme("tab-close", QIcon(":/new/prefix1/images/remove.png")));
    m_Ui->actionExportToHTML->setIcon(QIcon::fromTheme("globe", QIcon(":/new/prefix1/images/globe.png")));
    m_Ui->removeSelectionAction->setIcon(QIcon::fromTheme("edit-select-none", QIcon(":/new/prefix1/images/tool_rect_selection.png")));
    m_Ui->actionBatch_HDR->setIcon(QIcon::fromTheme("system-run", QIcon(":/new/prefix1/images/system-run.png"))); // TODO
    m_Ui->actionUpdateAvailable->setIcon(QIcon::fromTheme("system-software-update", QIcon(":/new/prefix1/images/vcs-update-required.png")));
    // end setting icons

    // About(s)
    connect(m_Ui->actionAbout_Qt,SIGNAL(triggered()),qApp,SLOT(aboutQt()));
    connect(m_Ui->actionWhat_s_This,SIGNAL(triggered()),this,SLOT(enterWhatsThis()));

    // I/O
    connect(m_Ui->fileExitAction, SIGNAL(triggered()), this, SLOT(close()));

    // Crop & Rotation
    connect(m_Ui->cropToSelectionAction, SIGNAL(triggered()), this, SLOT(cropToSelection()));
    m_Ui->cropToSelectionAction->setEnabled(false);

    connect(m_Ui->removeSelectionAction, SIGNAL(triggered()), this, SLOT(disableCrop()));

    connect(m_Ui->menuWindows, SIGNAL(aboutToShow()), this, SLOT(updateWindowMenu()));
    connect(m_Ui->actionMinimize, SIGNAL(triggered()), this, SLOT(showMinimized()));
    connect(m_Ui->actionMaximize, SIGNAL(triggered()), this, SLOT(showMaximized()));
    connect(m_Ui->actionShowPreviewPanel, SIGNAL(toggled(bool)), this, SLOT(showPreviewPanel(bool)));
    connect(m_Ui->actionShowPreviewPanel, SIGNAL(toggled(bool)), luminance_options, SLOT(setPreviewPanelActive(bool)));

    //recent files
    initRecentFileActions();
    updateRecentFileActions();
    
#ifndef HAVE_CFITSIO
    m_Ui->actionFits_Importer->setVisible(false);
#endif
}

void MainWindow::createStatusBar()
{
    statusBar()->showMessage(tr("Ready. Now open an existing HDR image or create a new one!"), 10000);
}

void MainWindow::createConnections()
{
    windowMapper = new QSignalMapper(this);
    connect(windowMapper, SIGNAL(mapped(QWidget*)), this, SLOT(setActiveMainWindow(QWidget*)));
    connect(&m_futureWatcher, SIGNAL(finished()), this, SLOT(whiteBalanceDone()));
}

void MainWindow::loadOptions()
{
    //load from settings the path where hdrs have been previously opened/loaded

    //load from settings the main toolbar visualization mode
    switch ( luminance_options->getMainWindowToolBarMode() ) {
    case Qt::ToolButtonIconOnly:
        Icons_Only();
        m_Ui->actionIcons_Only->setChecked(true);
	break;
    case Qt::ToolButtonTextOnly:
        Text_Only();
        m_Ui->actionText_Only->setChecked(true);
	break;
    case Qt::ToolButtonTextBesideIcon:
        Text_Alongside_Icons();
        m_Ui->actionText_Alongside_Icons->setChecked(true);
	break;
    case Qt::ToolButtonTextUnderIcon:
        Text_Under_Icons();
        m_Ui->actionText_Under_Icons->setChecked(true);
	break;
    }
    m_Ui->actionShowPreviewPanel->setChecked(luminance_options->isPreviewPanelActive());
    m_Ui->actionRealtimePreviews->setChecked(luminance_options->isRealtimePreviewsActive());

    bool isPreviewPanelRight = luminance_options->getPreviewPanelMode() == 0;
    m_Ui->actionShow_on_the_bottom->setChecked(!isPreviewPanelRight);
    m_Ui->actionShow_on_the_right->setChecked(isPreviewPanelRight);
}

void MainWindow::on_actionDonate_triggered()
{
    DonationDialog::openDonationPage();
}

void MainWindow::on_fileNewAction_triggered()
{
	createNewHdr(QStringList()); // redirect on createNewHdr-method to avoid moc warning
}

void MainWindow::createNewHdr(const QStringList& files)
{
    QScopedPointer<HdrWizard> wizard( new HdrWizard(this, files, m_inputFilesName, m_inputExpoTimes) );
    if (wizard->exec() == QDialog::Accepted)
    {
        emit load_success(wizard->getPfsFrameHDR(), wizard->getCaptionTEXT(), wizard->getInputFilesNames(), true);
    }
}

void MainWindow::on_fileOpenAction_triggered()
{
    QString filetypes = tr("All HDR formats ");
    QStringList hdrExtensionsList = getAllHdrFileExtensions();
    filetypes += "(";
    foreach(QString s, hdrExtensionsList)
    {
        filetypes += "*" + s + " ";
    }
    filetypes +=  ");;" ;
    filetypes += "OpenEXR (*.exr *.EXR);;" ;
    filetypes += "Radiance RGBE (*.hdr *.pic *.HDR *.PIC);;";
    filetypes += "TIFF images (*.TIFF *.TIF *.tiff *.tif);;";
#if HAVE_CFITSIO
    filetypes += "FITS (*.fit *.FIT *.fits *.FITS);;";
#endif
    filetypes += "RAW images (*.crw *.cr2 *.nef *.dng *.mrw *.orf *.kdc *.dcr *.arw *.raf *.ptx *.pef *.x3f *.raw *.rw2 *.sr2 *.3fr *.mef *.mos *.erf *.nrw *.mef *.mos *.erf *.nrw *.srw";
    filetypes +=             "*.CRW *.CR2 *.NEF *.DNG *.MRW *.ORF *.KDC *.DCR *.ARW *.RAF *.PTX *.PEF *.X3F *.RAW *.RW2 *.SR2 *.3FR *.MEF *.MOS *.ERF *.NRW *.SRW);;";
    filetypes += "PFS stream (*.pfs *.PFS)";

    QStringList files = QFileDialog::getOpenFileNames(this,
                                                      tr("Load one or more HDR images..."),
                                                      luminance_options->getDefaultPathHdrIn(),
                                                      filetypes );

    if ( files.isEmpty() ) return;

    // Update working folder
    // All the files are in the same folder, so I pick the first as reference to update the settings
    QFileInfo qfi(files.first());

    luminance_options->setDefaultPathHdrIn(qfi.absolutePath());

    foreach (const QString& filename, files)
    {
        // emit open_hdr_frame(filename);
        QMetaObject::invokeMethod(m_IOWorker, "read_hdr_frame", Qt::QueuedConnection,
                                  Q_ARG(QString, filename));
    }
}

void MainWindow::on_fileSaveAllAction_triggered()
{
    if (m_tabwidget->count() <= 0) return;

    QString dir = QFileDialog::getExistingDirectory(
                this,
                tr("Save files in"),
                luminance_options->getDefaultPathLdrOut()
                );

    if (!dir.isEmpty())
    {
        luminance_options->setDefaultPathLdrOut(dir);

        for (int i = 0; i < m_tabwidget->count(); i++)
        {
            QWidget *wgt = m_tabwidget->widget(i);
            GenericViewer *g_v = (GenericViewer *)wgt;

            if ( !g_v->isHDR() )
            {
                LdrViewer *l_v = dynamic_cast<LdrViewer*>(g_v);

                QString ldr_name = QFileInfo(getCurrentHDRName()).baseName();
                QString outfname = luminance_options->getDefaultPathLdrOut()
                        + "/" + ldr_name + "_" + l_v->getFileNamePostFix() + ".jpg";

                QMetaObject::invokeMethod(m_IOWorker, "write_ldr_frame", Qt::QueuedConnection,
                                          Q_ARG(GenericViewer*, l_v),
                                          Q_ARG(QString, outfname),
                                          Q_ARG(QString, QString()),
                                          Q_ARG(QVector<float>, QVector<float>()),
                                          Q_ARG(TonemappingOptions*, NULL),
                                          Q_ARG(pfs::Params, pfs::Params("quality", 100u)));
            }
        }
    }
}

void MainWindow::on_fileSaveAsAction_triggered()
{
    if (m_tabwidget->count() <= 0) return;

    GenericViewer* g_v = (GenericViewer*)m_tabwidget->currentWidget();
    if ( g_v->isHDR() ) {
        // In this case I'm saving an HDR
        QString fname = getHdrFileNameFromSaveDialog(g_v->getFileName(), this);

        if ( fname.isEmpty() ) return;

        QFileInfo qfi(fname);
        QString format = qfi.suffix();

        pfs::Params p;
        if ( format == "tif" || format == "tiff" )
        {
            TiffModeDialog t(true, -1, this);
            if ( t.exec() == QDialog::Rejected ) return;

            p.set("tiff_mode", t.getTiffWriterMode());
        }

        // CALL m_IOWorker->write_hdr_frame(dynamic_cast<HdrViewer*>(g_v), fname);
        QMetaObject::invokeMethod(m_IOWorker, "write_hdr_frame",
                                  Qt::QueuedConnection,
                                  Q_ARG(GenericViewer*, dynamic_cast<HdrViewer*>(g_v)),
                                  Q_ARG(QString, fname),
                                  Q_ARG(pfs::Params, p));
    }
    else {
        // In this case I'm saving an LDR

        LdrViewer* l_v = dynamic_cast<LdrViewer*>(g_v);

        if ( l_v == NULL ) return;

        QString ldr_name = QFileInfo(getCurrentHDRName()).baseName();

        QString outputFilename = getLdrFileNameFromSaveDialog(ldr_name + "_" + l_v->getFileNamePostFix() + ".jpg", this);

        if ( outputFilename.isEmpty() ) return;

        QString format = QFileInfo(outputFilename).suffix();

        if ( format.isEmpty() ) {   // default as JPG
            format          =   "jpg";
            outputFilename  +=  ".jpg";
        }

        pfs::Params p;
        if ( format == "png" || format == "jpg" )
        {
            ImageQualityDialog savedFileQuality(l_v->getFrame(), format, -1, this);
            savedFileQuality.setWindowTitle( QObject::tr("Save as...") + format.toUpper() );
            if ( savedFileQuality.exec() == QDialog::Rejected ) return;

            p.set("quality", (size_t)savedFileQuality.getQuality());
        }

        if ( format == "tif" || format == "tiff" )
        {
            TiffModeDialog t(false, -1, this);
            if ( t.exec() == QDialog::Rejected ) return;

            p.set("tiff_mode", t.getTiffWriterMode());
        }

        QString inputfname;
        if ( ! m_inputFilesName.isEmpty() ) inputfname = m_inputFilesName.first();

        QMetaObject::invokeMethod(m_IOWorker, "write_ldr_frame", Qt::QueuedConnection,
                                  Q_ARG(GenericViewer*, l_v),
                                  Q_ARG(QString, outputFilename),
                                  Q_ARG(QString, inputfname),
                                  Q_ARG(QVector<float>, m_inputExpoTimes),
                                  Q_ARG(TonemappingOptions*, l_v->getTonemappingOptions()),
                                  Q_ARG(pfs::Params, p));

    }
}

void MainWindow::save_hdr_success(GenericViewer* saved_hdr, const QString& fname)
{
    QFileInfo qfi(fname);

    setCurrentFile(qfi.absoluteFilePath());
    setWindowModified(false);

    // update name on the tab label
    m_tabwidget->setTabText(m_tabwidget->indexOf(saved_hdr), qfi.fileName());
}

void MainWindow::save_hdr_failed(const QString &fname)
{
    // TODO give some kind of feedback to the user!
    // TODO pass the name of the file, so the user know which file didn't save correctly
    // DONE!!! Once again, use unified style?
    QMessageBox::warning(0,"", tr("Failed to save %1").arg(fname), QMessageBox::Ok, QMessageBox::NoButton);
}

void MainWindow::save_ldr_success(GenericViewer* saved_ldr, const QString& fname)
{
    if ( !saved_ldr->isHDR() )
        m_tabwidget->setTabText(m_tabwidget->indexOf(saved_ldr), QFileInfo(fname).fileName());
}

void MainWindow::save_ldr_failed(const QString &fname)
{
    // TODO give some kind of feedback to the user!
    // TODO pass the name of the file, so the user know which file didn't save correctly
    // DONE!!! Once again, use unified style?
    QMessageBox::warning(0,"", tr("Failed to save %1").arg(fname), QMessageBox::Ok, QMessageBox::NoButton);
}

void MainWindow::on_actionSave_Hdr_Preview_triggered()
{
    if (m_tabwidget->count() <= 0) return;

    GenericViewer* g_v = (GenericViewer*)m_tabwidget->currentWidget();

    if (!g_v->isHDR()) return;
    try
    {
        QString ldr_name = QFileInfo(getCurrentHDRName()).baseName();

        QString outfname = getLdrFileNameFromSaveDialog(ldr_name + "_" + g_v->getFileNamePostFix() + ".jpg", this);

        if ( outfname.isEmpty() ) return;

        QMetaObject::invokeMethod(m_IOWorker, "write_ldr_frame", Qt::QueuedConnection,
                                  Q_ARG(GenericViewer*, g_v),
                                  Q_ARG(QString, outfname),
                                  Q_ARG(QString, QString()),
                                  Q_ARG(QVector<float>, QVector<float>()),
                                  Q_ARG(TonemappingOptions*, NULL),
                                  Q_ARG(pfs::Params, pfs::Params("quality", 100u)) );
    }
    catch (...)
    {
        return;
    }
}

void MainWindow::updateActions( int w )
{
#ifdef QT_DEBUG
    qDebug() << "MainWindow::updateActions(" << w << ")";
#endif
	bool hasImage = w >= 0;
	GenericViewer* g_v = hasImage ? (GenericViewer*)m_tabwidget->widget(w) : 0;
	bool isHdr = g_v ? g_v->isHDR() : false;
	bool isLdr = g_v ? !g_v->isHDR() : false;
	LuminanceOptions luminance_opts;
	bool hasPrinterProfile = !luminance_opts.getPrinterProfileFileName().isEmpty();

    updateMagnificationButtons(g_v); // g_v ? g_v : 0

    m_Ui->fileSaveAsAction->setEnabled(hasImage);
    m_Ui->actionRemove_Tab->setEnabled(hasImage);
    m_Ui->actionSave_Hdr_Preview->setEnabled(hasImage && isHdr);
    m_Ui->fileSaveAllAction->setEnabled(hasImage && curr_num_ldr_open >= 2);

    // Histogram
    m_Ui->menuHDR_Histogram->setEnabled(isHdr);
    m_Ui->Low_dynamic_range->setEnabled(isHdr);
    m_Ui->Fit_to_dynamic_range->setEnabled(isHdr);
    m_Ui->Shrink_dynamic_range->setEnabled(isHdr);
    m_Ui->Extend_dynamic_range->setEnabled(isHdr);
    m_Ui->Decrease_exposure->setEnabled(isHdr);
    m_Ui->Increase_exposure->setEnabled(isHdr);

    m_Ui->actionResizeHDR->setEnabled(isHdr);
    m_Ui->action_Projective_Transformation->setEnabled(isHdr);
    m_Ui->actionExportToHTML->setEnabled(isHdr);
    m_Ui->rotateccw->setEnabled(isHdr);
    m_Ui->rotatecw->setEnabled(isHdr);

    m_Ui->actionFix_Histogram->setEnabled(isLdr);
    m_Ui->actionWhite_Balance->setEnabled(hasImage && !m_processingAWB);
    m_Ui->actionSoft_Proofing->setEnabled(isLdr && hasPrinterProfile);
    m_Ui->actionGamut_Check->setEnabled(isLdr && hasPrinterProfile);

    bool hasCropping = isHdr && tm_status.curr_tm_frame && tm_status.curr_tm_frame->hasSelection();
    m_Ui->cropToSelectionAction->setEnabled(hasCropping);
    m_Ui->removeSelectionAction->setEnabled(hasCropping);

    m_Ui->actionLock->setEnabled(hasImage && curr_num_ldr_open >= 1);
    m_Ui->actionShow_Image_Full_Screen->setEnabled(hasImage);
}

void MainWindow::on_rotateccw_triggered()
{
    dispatchrotate(false);
}

void MainWindow::on_rotatecw_triggered()
{
    dispatchrotate(true);
}

void MainWindow::dispatchrotate(bool clockwise)
{
    if (m_tabwidget->count() <= 0) return;

    GenericViewer* curr_g_v = (GenericViewer*)m_tabwidget->currentWidget();

    m_Ui->rotateccw->setEnabled(false);
    m_Ui->rotatecw->setEnabled(false);

    QApplication::setOverrideCursor( QCursor(Qt::WaitCursor) );
    pfs::Frame *rotated = pfs::rotate(curr_g_v->getFrame(), clockwise);

    curr_g_v->setFrame(rotated);
    if ( !curr_g_v->needsSaving() )
    {
        curr_g_v->setNeedsSaving(true);

        int index = m_tabwidget->indexOf(curr_g_v);
        QString text = m_tabwidget->tabText(index);
        m_tabwidget->setTabText(index, text.prepend("(*) "));

        setWindowModified(true);
    }
    emit updatedHDR(curr_g_v->getFrame());
    QApplication::restoreOverrideCursor();

    m_Ui->rotateccw->setEnabled(true);
    m_Ui->rotatecw->setEnabled(true);
}

void MainWindow::on_actionResizeHDR_triggered()
{
    if (m_tabwidget->count() <= 0) return;

    GenericViewer* curr_g_v = (GenericViewer*)m_tabwidget->currentWidget();

    ResizeDialog *resizedialog = new ResizeDialog(this, curr_g_v->getFrame());
    if (resizedialog->exec() == QDialog::Accepted)
    {
        QApplication::setOverrideCursor( QCursor(Qt::WaitCursor) );

        curr_g_v->setFrame(resizedialog->getResizedFrame());
        if (! curr_g_v->needsSaving())
        {
            curr_g_v->setNeedsSaving(true);

            int index = m_tabwidget->indexOf(curr_g_v);
            QString text = m_tabwidget->tabText(index);
            m_tabwidget->setTabText(index, text.prepend("(*) "));

            setWindowModified(true);
        }
        emit updatedHDR(curr_g_v->getFrame());
        QApplication::restoreOverrideCursor();
    }
    delete resizedialog;
}

void MainWindow::on_actionExportToHTML_triggered()
{
    if (m_tabwidget->count() <= 0) return;

    GenericViewer* curr_g_v = (GenericViewer*)m_tabwidget->currentWidget();
    ExportToHtmlDialog *exportDialog = new ExportToHtmlDialog(this, curr_g_v->getFrame());

    exportDialog->exec();
    delete exportDialog;
}

void MainWindow::on_actionSupported_Cameras_triggered()
{
    SupportedCamerasDialog *supportedDialog = new SupportedCamerasDialog(this);

    supportedDialog->exec();
    delete supportedDialog;
}

void MainWindow::on_action_Projective_Transformation_triggered()
{
    if (m_tabwidget->count() <= 0) return;

    GenericViewer* curr_g_v = (GenericViewer*)m_tabwidget->currentWidget();

    ProjectionsDialog *projTranfsDialog = new ProjectionsDialog(this, curr_g_v->getFrame());
    if (projTranfsDialog->exec() == QDialog::Accepted)
    {
        QApplication::setOverrideCursor( QCursor(Qt::WaitCursor) );

        curr_g_v->setFrame(projTranfsDialog->getTranformedFrame());
        if ( !curr_g_v->needsSaving() )
        {
            curr_g_v->setNeedsSaving(true);

            int index = m_tabwidget->indexOf(curr_g_v);
            QString text = m_tabwidget->tabText(index);
            m_tabwidget->setTabText(index, text.prepend("(*) "));

            setWindowModified(true);
        }
        emit updatedHDR(curr_g_v->getFrame());
        QApplication::restoreOverrideCursor();
    }
    delete projTranfsDialog;
}

void MainWindow::on_Decrease_exposure_triggered()
{
    if (m_tabwidget->count() <= 0) return;

    GenericViewer* g_v = (GenericViewer*)m_tabwidget->currentWidget();
    HdrViewer* curr_hdr_v = dynamic_cast<HdrViewer*>(g_v);
    if ( curr_hdr_v != NULL )
        curr_hdr_v->lumRange()->decreaseExposure();
}

void MainWindow::on_Extend_dynamic_range_triggered()
{
    if (m_tabwidget->count() <= 0) return;

    GenericViewer* g_v = (GenericViewer*)m_tabwidget->currentWidget();
    HdrViewer* curr_hdr_v = dynamic_cast<HdrViewer*>(g_v);
    if ( curr_hdr_v != NULL )
        curr_hdr_v->lumRange()->extendRange();
}

void MainWindow::on_Fit_to_dynamic_range_triggered()
{
    if (m_tabwidget->count() <= 0) return;

    GenericViewer* g_v = (GenericViewer*)m_tabwidget->currentWidget();
    HdrViewer* curr_hdr_v = dynamic_cast<HdrViewer*>(g_v);
    if ( curr_hdr_v != NULL )
        curr_hdr_v->lumRange()->fitToDynamicRange();
}

void MainWindow::on_Increase_exposure_triggered()
{
    if (m_tabwidget->count() <= 0) return;

    GenericViewer* g_v = (GenericViewer*)m_tabwidget->currentWidget();
    HdrViewer* curr_hdr_v = dynamic_cast<HdrViewer*>(g_v);
    if ( curr_hdr_v != NULL )
        curr_hdr_v->lumRange()->increaseExposure();
}

void MainWindow::on_Shrink_dynamic_range_triggered()
{
    if (m_tabwidget->count() <= 0) return;

    GenericViewer* g_v = (GenericViewer*)m_tabwidget->currentWidget();
    HdrViewer* curr_hdr_v = dynamic_cast<HdrViewer*>(g_v);
    if ( curr_hdr_v != NULL )
        curr_hdr_v->lumRange()->shrinkRange();
}

void MainWindow::on_Low_dynamic_range_triggered()
{
    if (m_tabwidget->count() <= 0) return;

    GenericViewer* g_v = (GenericViewer*)m_tabwidget->currentWidget();
    HdrViewer* curr_hdr_v = dynamic_cast<HdrViewer*>(g_v);
    if ( curr_hdr_v != NULL )
        curr_hdr_v->lumRange()->lowDynamicRange();
}

// Zoom = Viewers (START)
void MainWindow::on_zoomInAct_triggered()
{
    if (m_tabwidget->count() <= 0) return;

    GenericViewer* g_v = (GenericViewer*)m_tabwidget->currentWidget();

    g_v->zoomIn();
    //updateMagnificationButtons(g_v);
}

void MainWindow::on_zoomOutAct_triggered()
{
    if (m_tabwidget->count() <= 0) return;

    GenericViewer* g_v = (GenericViewer*)m_tabwidget->currentWidget();

    g_v->zoomOut();
    //updateMagnificationButtons(g_v);
}

void MainWindow::on_fitToWindowAct_triggered()
{
    if (m_tabwidget->count() <= 0) return;

    GenericViewer* g_v = (GenericViewer*)m_tabwidget->currentWidget();

    g_v->fitToWindow();
    //updateMagnificationButtons(g_v);
}

void MainWindow::on_actionFill_to_Window_triggered()
{
    if (m_tabwidget->count() <= 0) return;

    GenericViewer* g_v = (GenericViewer*)m_tabwidget->currentWidget();

    g_v->fillToWindow();
    //updateMagnificationButtons(g_v);
}

void MainWindow::on_normalSizeAct_triggered()
{
    if (m_tabwidget->count() <= 0) return;

    GenericViewer* g_v = (GenericViewer*)m_tabwidget->currentWidget();

    g_v->normalSize();
    //updateMagnificationButtons(g_v);
}
// Zoom = Viewers (END)


void MainWindow::on_documentationAction_triggered()
{
    helpBrowser = new HelpBrowser(this,"Luminance HDR Help");
    helpBrowser->setAttribute(Qt::WA_DeleteOnClose);
    connect(helpBrowser, SIGNAL(closed()), this, SLOT(helpBrowserClosed()));
    helpBrowser->show();
}

void MainWindow::helpBrowserClosed()
{
    helpBrowser = NULL;
}

void MainWindow::enterWhatsThis()
{
    QWhatsThis::enterWhatsThisMode();
}

void MainWindow::openRecentFile()
{
    QAction *action = qobject_cast<QAction *>(sender());
    if (action)
    {
        //emit open_hdr_frame(action->data().toString());
        QMetaObject::invokeMethod(m_IOWorker, "read_hdr_frame", Qt::QueuedConnection,
                                  Q_ARG(QString, action->data().toString()));
    }
}

void MainWindow::setupIO()
{
    // progress bar
    m_ProgressBar = new QProgressBar(this);
    m_ProgressBar->hide();
    statusBar()->addWidget(m_ProgressBar);

    // Init Object/Thread
    m_IOThread = new QThread;
    m_IOWorker = new IOWorker;

    m_IOWorker->moveToThread(m_IOThread);

    // Memory Management
    connect(this, SIGNAL(destroyed()), m_IOWorker, SLOT(deleteLater()));
    connect(m_IOWorker, SIGNAL(destroyed()), m_IOThread, SLOT(deleteLater()));

    // Open
    //connect(this, SIGNAL(open_hdr_frame(QString)), m_IOWorker, SLOT(read_hdr_frame(QString)));
    connect(m_IOWorker, SIGNAL(read_hdr_success(pfs::Frame*, QString)), this, SLOT(load_success(pfs::Frame*, QString)));
    connect(m_IOWorker, SIGNAL(read_hdr_failed(QString)), this, SLOT(load_failed(QString)));

    // Save HDR
    //connect(this, SIGNAL(save_hdr_frame(HdrViewer*, QString)), m_IOWorker, SLOT(write_hdr_frame(HdrViewer*, QString)));
    connect(m_IOWorker, SIGNAL(write_hdr_success(GenericViewer*, QString)), this, SLOT(save_hdr_success(GenericViewer*, QString)));
    connect(m_IOWorker, SIGNAL(write_hdr_failed(QString)), this, SLOT(save_hdr_failed(QString)));
    // Save LDR
    //connect(this, SIGNAL(save_ldr_frame(LdrViewer*, QString, int)), m_IOWorker, SLOT(write_ldr_frame(LdrViewer*, QString, int)));
    connect(m_IOWorker, SIGNAL(write_ldr_success(GenericViewer*, QString)), this, SLOT(save_ldr_success(GenericViewer*, QString)));
    connect(m_IOWorker, SIGNAL(write_ldr_failed(QString)), this, SLOT(save_ldr_failed(QString)));

    // progress bar handling
    connect(m_IOWorker, SIGNAL(setValue(int)), m_ProgressBar, SLOT(setValue(int)));
    connect(m_IOWorker, SIGNAL(setMaximum(int)), m_ProgressBar, SLOT(setMaximum(int)));
    connect(m_IOWorker, SIGNAL(IO_init()), this, SLOT(ioBegin()));
    connect(m_IOWorker, SIGNAL(IO_finish()), this, SLOT(ioEnd()));

    // start thread waiting for signals (I/O requests)
    m_IOThread->start();
}

void MainWindow::ioBegin()
{
    statusBar()->clearMessage();

    QApplication::setOverrideCursor( QCursor(Qt::WaitCursor) );

    //  statusBar()->addWidget(m_ProgressBar);
    m_ProgressBar->setMaximum(0);
    m_ProgressBar->show();
}

void MainWindow::ioEnd()
{
    //statusBar()->removeWidget(m_ProgressBar);
    m_ProgressBar->reset();
    m_ProgressBar->hide();

    QApplication::restoreOverrideCursor();

    statusBar()->showMessage(tr("Done!"), 800);
}

void MainWindow::load_failed(const QString& errorMessage)
{
    m_ProgressBar->reset();
    m_ProgressBar->hide();

    QApplication::restoreOverrideCursor();

    // TODO: use unified style?
    QMessageBox::critical(this, tr("Aborting..."), errorMessage,
                          QMessageBox::Ok, QMessageBox::NoButton);
}

void MainWindow::load_success(pfs::Frame* new_hdr_frame,
                              const QString& new_fname, const QStringList& inputFileNames, bool needSaving)
{
    if ( tm_status.is_hdr_ready )
    {
        MainWindow *other = new MainWindow(new_hdr_frame, new_fname, inputFileNames, needSaving);
        other->move(x() + 40, y() + 40);
        other->show();
    }
    else
    {
#ifdef QT_DEBUG
        qDebug() << "Filename: " << new_fname;
#endif

        HdrViewer* newhdr = new HdrViewer(new_hdr_frame, this, needSaving);

        newhdr->setAttribute(Qt::WA_DeleteOnClose);

        connect(newhdr, SIGNAL(selectionReady(bool)),
                this, SLOT(enableCrop(bool)));
        connect(newhdr, SIGNAL(changed(GenericViewer*)),
                this, SLOT(syncViewers(GenericViewer*)));
        connect(newhdr, SIGNAL(changed(GenericViewer*)),
                this, SLOT(updateMagnificationButtons(GenericViewer*)));

        newhdr->setViewerMode( getCurrentViewerMode(*m_tabwidget) );

        QFileInfo qfileinfo(new_fname);
        if ( !qfileinfo.exists() )
        {
            // it doesn't exist on the file system, so I have just got back a
            // file from some creational operation (new hdr, crop...)

            newhdr->setFileName(QString(tr("Untitled")));
            m_tabwidget->addTab(newhdr, QString(new_fname).prepend("(*) "));

            setMainWindowModified(true);
        }
        else
        {
            // the new file exists on the file system, so I can use this value
            // to set captions and so on
            newhdr->setFileName(new_fname);
            m_tabwidget->addTab(newhdr, qfileinfo.fileName());

            setCurrentFile(new_fname);
        }

        m_inputFilesName = inputFileNames;

        tm_status.is_hdr_ready = true;
        tm_status.curr_tm_frame = newhdr;

        m_tabwidget->setCurrentWidget(newhdr);

        m_tonemapPanel->setEnabled(true);
        m_tonemapPanel->updatedHDR(new_hdr_frame);
        m_Ui->actionShowPreviewPanel->setEnabled(true);

        showPreviewPanel(m_Ui->actionShowPreviewPanel->isChecked());

        // signal: I have a new HDR open
        emit updatedHDR(newhdr->getFrame());
    }
}

void MainWindow::on_OptionsAction_triggered()
{
    PreferencesDialog *opts = new PreferencesDialog(this);
    opts->setAttribute(Qt::WA_DeleteOnClose);
    if (opts->exec() == QDialog::Accepted)
    {
        m_Ui->actionShowPreviewPanel->setChecked(luminance_options->isPreviewPanelActive());
    }
}

void MainWindow::on_Transplant_Exif_Data_action_triggered()
{
    TransplantExifDialog *transplant=new TransplantExifDialog(this);
    transplant->setAttribute(Qt::WA_DeleteOnClose);
    transplant->exec();
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    if (event->mimeData()->hasUrls())
    {
        openFiles(convertUrlListToFilenameList(event->mimeData()->urls()));
    }
    event->acceptProposedAction();
}

void MainWindow::openFile(const QString& filename)
{
    QMetaObject::invokeMethod(m_IOWorker, "read_hdr_frame",
                              Qt::QueuedConnection,
                              Q_ARG(QString, filename));
}

void MainWindow::openFiles(const QStringList& files)
{
    if (files.size() > 0)
    {
        switch (DnDOptionDialog::showDndDialog(this, files))
        {
        case DnDOptionDialog::ACTION_NEW_HDR:
        {
            createNewHdr(files);
        } break;
        case DnDOptionDialog::ACTION_OPEN_HDR:
        {
            foreach (const QString& filename, files)
            {
                openFile(filename);
            }
        } break;
        }
    }
}

void MainWindow::Text_Under_Icons()
{
    m_Ui->toolBar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    luminance_options->setMainWindowToolBarMode(Qt::ToolButtonTextUnderIcon);
}

void MainWindow::Icons_Only()
{
    m_Ui->toolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    luminance_options->setMainWindowToolBarMode(Qt::ToolButtonIconOnly);
}

void MainWindow::Text_Alongside_Icons()
{
    m_Ui->toolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    luminance_options->setMainWindowToolBarMode(Qt::ToolButtonTextBesideIcon);
}

void MainWindow::Text_Only()
{
    m_Ui->toolBar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    luminance_options->setMainWindowToolBarMode(Qt::ToolButtonTextOnly);
}

void MainWindow::onUpdateAvailable()
{
    m_Ui->actionUpdateAvailable->setVisible(true);
}

void MainWindow::on_actionUpdateAvailable_triggered()
{
    if (sm_updateChecker)
    {
        sm_updateChecker->trayMessageClicked();
    }
}

void MainWindow::on_actionAbout_Luminance_triggered()
{
	UMessageBox::about();
}

/*
 * Window Menu Display and Functionalities
 */
void MainWindow::updateWindowMenu()
{
    // Remove current elements inside the menuWindows
    foreach (QAction* Action_MW, openMainWindows)
    {
        openMainWindows.removeAll(Action_MW);
        m_Ui->menuWindows->removeAction( Action_MW );
        delete Action_MW;
    }

    foreach (QWidget *widget, QApplication::topLevelWidgets())
    {
        MainWindow *MW = qobject_cast<MainWindow *>(widget);
        if (MW != NULL)
        {
            QAction *action  = m_Ui->menuWindows->addAction( MW->getCurrentHDRName() );

            action->setCheckable(true);
            action->setChecked(MW == this);
            connect(action, SIGNAL(triggered()), windowMapper, SLOT(map()));
            windowMapper->setMapping(action, MW);

            openMainWindows.push_back(action);
        }
    }
}

/*
 * This function sets the active Main Window
 * when the file name is selected inside the "Window" menu
 */
void MainWindow::setActiveMainWindow(QWidget* w)
{
    MainWindow *MW = qobject_cast<MainWindow *>(w);

    if ( MW == NULL ) return;

    MW->raise();
    MW->activateWindow();
    return;
}

void MainWindow::on_actionBring_All_to_Front_triggered()
{
    foreach (QWidget *widget, QApplication::topLevelWidgets())
    {
        MainWindow *MW = qobject_cast<MainWindow *>(widget);
        if (MW != NULL)
        {
            MW->raise();
        }
    }
}

void MainWindow::on_actionBatch_HDR_triggered()
{
    BatchHDRDialog *batch_hdr_dialog = new BatchHDRDialog(this);
    batch_hdr_dialog->exec();
    delete batch_hdr_dialog;
}

void MainWindow::on_actionBatch_Tone_Mapping_triggered()
{
    BatchTMDialog *batchdialog = new BatchTMDialog(this);
    batchdialog->exec();
    delete batchdialog;
}

void MainWindow::cropToSelection()
{
    if (m_tabwidget->count() <= 0) return;

    GenericViewer* curr_g_v = (GenericViewer*)m_tabwidget->currentWidget();

    if ( !curr_g_v->isHDR() ) return;
    if ( !curr_g_v->hasSelection() ) return;

    QRect cropRect = curr_g_v->getSelectionRect();
    int x_ul, y_ul, x_br, y_br;
    cropRect.getCoords(&x_ul, &y_ul, &x_br, &y_br);
    disableCrop();
    pfs::Frame *original_frame = curr_g_v->getFrame();
    pfs::Frame *cropped_frame = pfs::cut(original_frame, x_ul, y_ul, x_br, y_br);

    emit load_success(cropped_frame, QString(tr("Cropped Image")), m_inputFilesName, true);

    curr_g_v->removeSelection();
}

void MainWindow::enableCrop(bool isReady)
{
    m_Ui->cropToSelectionAction->setEnabled(isReady);
    m_Ui->removeSelectionAction->setEnabled(isReady);
}

void MainWindow::disableCrop()
{
    if (m_tabwidget->count() <= 0) return;

    GenericViewer* curr_g_v = (GenericViewer*)m_tabwidget->currentWidget();

    if ( !curr_g_v->isHDR() ) return;

    curr_g_v->removeSelection();
    m_Ui->cropToSelectionAction->setEnabled(false);
    m_Ui->removeSelectionAction->setEnabled(false);
}

void MainWindow::changeEvent(QEvent *event)
{
	if (event->type() == QEvent::LanguageChange)
		 m_Ui->retranslateUi(this);
	QWidget::changeEvent(event);
}

void MainWindow::closeEvent( QCloseEvent *event )
{
    if ( maybeSave() )
    {
        event->accept();
    }
    else
    {
        event->ignore();
    }
}

bool MainWindow::event(QEvent* event)
{
	bool result = QMainWindow::event(event);
    if (event->type() == QEvent::WindowActivate && m_firstWindow == 1)
    {
        m_firstWindow = 2;
        if (luminance_options->doShowWindowsOnWindows64Message())
        {
    		QMessageBox::warning(this, "Luminance HDR 32-bit on 64-bit", tr("It appears that you are running the 32-bit version <strong>Luminance HDR</strong> on a 64-bit system. <br>Please download the <strong>64-bit</strong> version from <a href=\"http://qtpfsgui.sourceforge.net\">http://qtpfsgui.sourceforge.net</a> to get the best Luminance HDR experience!"), QMessageBox::Ok, QMessageBox::NoButton);        
        }
	}
	return result;
}

bool MainWindow::maybeSave()
{
    // if no HDR is open, return true
    if ( !tm_status.is_hdr_ready ) return true;

    if ( tm_status.curr_tm_frame->needsSaving() )
    {
        int ret = UMessageBox::saveDialog(
                    tr("Unsaved changes..."),
                    tr("This HDR image has unsaved changes.<br>Do you want to save it?"),
                    this);
        switch(ret)
        {
        case QMessageBox::Save: {
            // if save == success return true;
            // else return false;
            QString fname = getHdrFileNameFromSaveDialog(QString("Untitled"), this);

            if ( !fname.isEmpty() )
            {
                // TODO : can I launch a signal and wait that it gets executed fully?
                return m_IOWorker->write_hdr_frame(
                            dynamic_cast<HdrViewer*>(tm_status.curr_tm_frame),
                            fname);
            }
            else
            {
                return false;
            }
        } break;
        case QMessageBox::Discard: {
            return true;
        } break;
        case QMessageBox::Cancel:
        default: {
            return false;
        } break;
        }
    }
    else return true;
}

void MainWindow::setupTM()
{
    // TODO: Building TM Thread
    tm_status.is_hdr_ready = false;
    tm_status.curr_tm_frame = NULL;
    tm_status.curr_tm_options = NULL;

    m_tonemapPanel->setEnabled(false);

    m_TMProgressBar = new TMOProgressIndicator;
    m_TMProgressBar->hide();
    statusBar()->addWidget(m_TMProgressBar);

    connect(this, SIGNAL(destroyed()), m_TMProgressBar, SLOT(deleteLater()));

    m_TMWorker = new TMWorker;
    m_TMThread = new QThread;

    m_TMWorker->moveToThread(m_TMThread);

    // Memory Management
    connect(this, SIGNAL(destroyed()), m_TMWorker, SLOT(deleteLater()));
    connect(m_TMWorker, SIGNAL(destroyed()), m_TMThread, SLOT(deleteLater()));

    // get back result!
    connect(m_TMWorker, SIGNAL(tonemapSuccess(pfs::Frame*,TonemappingOptions*)),
            this, SLOT(addLdrFrame(pfs::Frame*, TonemappingOptions*)));
    connect(m_TMWorker, SIGNAL(tonemapFailed(QString)),
            this, SLOT(tonemapFailed(QString)));

    // progress bar handling
    connect(m_TMWorker, SIGNAL(tonemapBegin()), this, SLOT(tonemapBegin()));
    connect(m_TMWorker, SIGNAL(tonemapEnd()), this, SLOT(tonemapEnd()));

    connect(m_TMWorker, SIGNAL(tonemapSetValue(int)), m_TMProgressBar, SLOT(setValue(int)));
    connect(m_TMWorker, SIGNAL(tonemapSetMaximum(int)), m_TMProgressBar, SLOT(setMaximum(int)));
    connect(m_TMWorker, SIGNAL(tonemapSetMinimum(int)), m_TMProgressBar, SLOT(setMinimum(int)));
    connect(m_TMProgressBar, SIGNAL(terminate()), m_TMWorker, SIGNAL(tonemapRequestTermination()), Qt::DirectConnection);

    // start thread waiting for signals (I/O requests)
    m_TMThread->start();
}

void MainWindow::setupQueue()
{
	m_QueueProgressBar = new TMOProgressIndicator;
	m_QueueProgressBar->hide();
	statusBar()->addWidget(m_QueueProgressBar);

	connect(this, SIGNAL(destroyed()), m_QueueProgressBar, SLOT(deleteLater()));

	m_QueueWorker = new TMWorker;
	m_QueueThread = new QThread;

	m_QueueWorker->moveToThread(m_QueueThread);

	// Memory Management
	connect(this, SIGNAL(destroyed()), m_QueueWorker, SLOT(deleteLater()));
	connect(m_QueueWorker, SIGNAL(destroyed()), m_QueueThread, SLOT(deleteLater()));

	// get back result!
	//connect(m_QueueWorker, SIGNAL(tonemapSuccess(pfs::Frame*, TonemappingOptions*)),
	//	this, SLOT(addLdrFrame(pfs::Frame*, TonemappingOptions*)));
	connect(m_QueueWorker, SIGNAL(tonemapFailed(QString)),
		this, SLOT(tonemapFailed(QString)));

	// progress bar handling
	connect(m_QueueWorker, SIGNAL(tonemapBegin()), this, SLOT(exportBegin()));
	connect(m_QueueWorker, SIGNAL(tonemapEnd()), this, SLOT(exportEnd()));

	connect(m_QueueWorker, SIGNAL(tonemapSetValue(int)), m_QueueProgressBar, SLOT(setValue(int)));
	connect(m_QueueWorker, SIGNAL(tonemapSetMaximum(int)), m_QueueProgressBar, SLOT(setMaximum(int)));
	connect(m_QueueWorker, SIGNAL(tonemapSetMinimum(int)), m_QueueProgressBar, SLOT(setMinimum(int)));
	connect(m_QueueProgressBar, SIGNAL(terminate()), m_QueueWorker, SIGNAL(tonemapRequestTermination()), Qt::DirectConnection);

	// start thread waiting for signals (I/O requests)
	m_QueueThread->start();
}

void MainWindow::tonemapBegin()
{
    // statusBar()->addWidget(m_TMProgressBar);
    m_TMProgressBar->setMaximum(0);
    m_TMProgressBar->show();
}

void MainWindow::tonemapEnd()
{
    // statusBar()->removeWidget(m_TMProgressBar);
    m_TMProgressBar->hide();
    m_TMProgressBar->reset();
}

void MainWindow::exportBegin()
{
	m_QueueProgressBar->setMaximum(0);
	m_QueueProgressBar->show();
}

void MainWindow::exportEnd()
{
	m_tonemapPanel->setExportQueueSize(--m_exportQueueSize);
	m_QueueProgressBar->hide();
	m_QueueProgressBar->reset();
}


void MainWindow::tonemapImage(TonemappingOptions *opts)
{
#ifdef QT_DEBUG
    qDebug() << "Start Tone Mapping";
#endif

    // Warning when using size dependent TMOs with smaller sizes
    if ( (opts->tmoperator == fattal) &&
         (opts->operator_options.fattaloptions.fftsolver == false) &&
         (opts->xsize != opts->origxsize) &&
         (luminance_options->isShowFattalWarning()) )
    {
        UMessageBox warningDialog(this);
        warningDialog.setText( tr("Fattal Warning") );
        warningDialog.setInformativeText( tr("This tonemapping operator depends on the size of the input "\
                                " image. Applying this operator on the full size image will "\
                                "most probably result in a different image. "\
                                "\n\nDo you want to continue?") );
        warningDialog.setStandardButtons(QMessageBox::Yes | QMessageBox::YesToAll | QMessageBox::No);
        warningDialog.setDefaultButton(QMessageBox::No);
        warningDialog.setIcon(QMessageBox::Warning);


        switch ( warningDialog.exec() )
        {
        case QMessageBox::Yes :
        {} break;
        case QMessageBox::YesAll :
        {
            luminance_options->setShowFattalWarning(false);
        } break;
        case QMessageBox::No :
        default:
        {
            return;
        }
        }
    }

    m_PreviewPanel->setEnabled(false);

    tm_status.curr_tm_options = opts;

    if ( tm_status.curr_tm_frame->hasSelection() )
    {
        opts->tonemapSelection = true;

        getCropCoords(tm_status.curr_tm_frame,
                      opts->selection_x_up_left,
                      opts->selection_y_up_left,
                      opts->selection_x_bottom_right,
                      opts->selection_y_bottom_right);
    }
    else
        opts->tonemapSelection = false;

    HdrViewer* hdr_viewer = dynamic_cast<HdrViewer*>(tm_status.curr_tm_frame);
    if ( hdr_viewer )
    {
#ifdef QT_DEBUG
        qDebug() << "MainWindow(): emit getTonemappedFrame()";
#endif
        //CALL m_TMWorker->getTonemappedFrame(hdr_viewer->getHDRPfsFrame(), opts);
        QMetaObject::invokeMethod(m_TMWorker, "computeTonemap", Qt::QueuedConnection,
                                  Q_ARG(pfs::Frame*, hdr_viewer->getFrame()), Q_ARG(TonemappingOptions*,opts));
    }
}

void MainWindow::exportImage(TonemappingOptions *opts)
{
    HdrViewer* hdr_viewer = dynamic_cast<HdrViewer*>(tm_status.curr_tm_frame);
    if ( hdr_viewer )
    {
        Params params = pfsadditions::FormatHelper::getParamsFromSettings(*luminance_options, KEY_FILEFORMAT_QUEUE, false);
        QString hdrName = QFileInfo(getCurrentHDRName()).baseName();

        QString inputfname;
        if ( ! m_inputFilesName.isEmpty() )
            inputfname = m_inputFilesName.first();

        QString exportDir = luminance_options->getExportDir();

		m_tonemapPanel->setExportQueueSize(++m_exportQueueSize);

		QMetaObject::invokeMethod(m_QueueWorker, "computeTonemapAndExport", Qt::QueuedConnection,
                                  Q_ARG(pfs::Frame*, hdr_viewer->getFrame()),
                                  Q_ARG(TonemappingOptions*,opts),
                                  Q_ARG(pfs::Params,params),
                                  Q_ARG(QString, exportDir),
                                  Q_ARG(QString, hdrName),
                                  Q_ARG(QString, inputfname),
                                  Q_ARG(QVector<float>, m_inputExpoTimes)
                              );
    }
}


void MainWindow::addLdrFrame(pfs::Frame *frame, TonemappingOptions* tm_options)
{
    if (m_tonemapPanel->getAutoLevels()) {
        float minL, maxL, gammaL;
        QScopedPointer<QImage> temp_qimage( fromLDRPFStoQImage(frame) );
        computeAutolevels(temp_qimage.data(), minL, maxL, gammaL);
        pfs::gammaAndLevels(frame, minL, maxL, 0.f, 1.f, gammaL);
    }

    GenericViewer *n = static_cast<GenericViewer*>(m_tabwidget->currentWidget());
    if (m_tonemapPanel->replaceLdr() && n != NULL && !n->isHDR())
    {
        n->setFrame(frame, tm_options);
    }
    else
    {
        curr_num_ldr_open++;
        num_ldr_generated++;

        n = new LdrViewer(frame, tm_options, this, true);

        connect(n, SIGNAL(changed(GenericViewer *)), this, SLOT(syncViewers(GenericViewer *)));
        connect(n, SIGNAL(changed(GenericViewer*)), this, SLOT(updateMagnificationButtons(GenericViewer*)));

        if (num_ldr_generated == 1)
            m_tabwidget->addTab(n, tr("Untitled"));
        else
            m_tabwidget->addTab(n, tr("Untitled %1").arg(num_ldr_generated));

        n->setViewerMode( getCurrentViewerMode( *m_tabwidget ) );

    }
    m_tabwidget->setCurrentWidget(n);

    m_PreviewPanel->setEnabled(true);

	if (m_Ui->actionSoft_Proofing->isChecked()) {
		LdrViewer *viewer = static_cast<LdrViewer *>(n);
		viewer->doSoftProofing(false);
	}
	else if (m_Ui->actionGamut_Check->isChecked()) {
		LdrViewer *viewer = static_cast<LdrViewer *>(n);
		viewer->doSoftProofing(true);
	}
}

void MainWindow::tonemapFailed(const QString& error_msg)
{
    QMessageBox::critical(this, tr("Luminance HDR"),
                          tr("Error: %1").arg(error_msg),
                          QMessageBox::Ok, QMessageBox::NoButton);

    m_tonemapPanel->setEnabled(true);
    m_TMProgressBar->hide();
}

/*
 * Lock Handling
 */
void MainWindow::on_actionLock_toggled(bool /*toggled*/)
{
    if (m_Ui->actionLock->isChecked() && m_tabwidget->count())
    {
        syncViewers((GenericViewer*)m_tabwidget->currentWidget());
    }
}

void MainWindow::syncViewers(GenericViewer *sender)
{
    if (sender == NULL) return;
    if (!m_Ui->actionLock->isChecked()) return;

    for (int idx = 0; idx < m_tabwidget->count(); idx++)
    {
        GenericViewer *viewer = (GenericViewer*)m_tabwidget->widget(idx);
        if (viewer == NULL) return; //this happens when a tab is removed and sync viewers is active, fixes a crash
        if (sender != viewer)
        {
            viewer->blockSignals(true);
            //disconnect(viewer,SIGNAL(changed(GenericViewer *)),this,SLOT(syncViewers(GenericViewer *)));
            viewer->syncViewer(sender);
            //connect(viewer,SIGNAL(changed(GenericViewer *)),this,SLOT(syncViewers(GenericViewer *)));
            viewer->blockSignals(false);
        }
    }
}

void MainWindow::showPreviewPanel(bool b)
{
    if (b)
    {
        if (tm_status.is_hdr_ready)
        {
            m_PreviewscrollArea->show();

            // ask panel to refresh itself
            m_PreviewPanel->setAutolevels(m_tonemapPanel->getAutoLevels());
            m_PreviewPanel->updatePreviews(tm_status.curr_tm_frame->getFrame());

            // connect signals
            connect(this, SIGNAL(updatedHDR(pfs::Frame*)), m_PreviewPanel, SLOT(updatePreviews(pfs::Frame*)));
            connect(m_PreviewPanel, SIGNAL(startTonemapping(TonemappingOptions*)), this, SLOT(tonemapImage(TonemappingOptions*)));
            connect(m_PreviewPanel, SIGNAL(startTonemapping(TonemappingOptions*)), m_tonemapPanel, SLOT(updateTonemappingParams(TonemappingOptions*)));
            connect(m_tonemapPanel, SIGNAL(autoLevels(bool)), this, SLOT(updatePreviews(bool)));
        }
    }
    else
    {
        m_PreviewscrollArea->hide();

        // disconnect signals
        disconnect(this, SIGNAL(updatedHDR(pfs::Frame*)), m_PreviewPanel, SLOT(updatePreviews(pfs::Frame*)));
        disconnect(m_PreviewPanel, SIGNAL(startTonemapping(TonemappingOptions*)), this, SLOT(tonemapImage(TonemappingOptions*)));
        disconnect(m_PreviewPanel, SIGNAL(startTonemapping(TonemappingOptions*)), m_tonemapPanel, SLOT(updateTonemappingParams(TonemappingOptions*)));
        disconnect(m_tonemapPanel, SIGNAL(autoLevels(bool)), this, SLOT(updatePreviews(bool)));
    }
}

void MainWindow::updatePreviews(bool b)
{
    if (m_Ui->actionShowPreviewPanel->isChecked())
    {
        if (tm_status.is_hdr_ready)
        {
            m_PreviewPanel->setAutolevels(b);
            // ask panel to refresh itself
            m_PreviewPanel->updatePreviews(tm_status.curr_tm_frame->getFrame());
        }
    }
}

void MainWindow::updateMagnificationButtons(GenericViewer* c_v)
{
	bool hasImage = c_v != NULL;
	bool isNormalSize = c_v && c_v->isNormalSize();
	bool isFilledToWindow = c_v && c_v->isFilledToWindow();
	bool isFittedToWindow = c_v && c_v->isFittedToWindow();

    m_Ui->zoomInAct->setEnabled(hasImage && (isFilledToWindow || isFittedToWindow));
    m_Ui->zoomOutAct->setEnabled(hasImage && !isFittedToWindow);

    m_Ui->normalSizeAct->setChecked(hasImage && isNormalSize);
    m_Ui->normalSizeAct->setEnabled(hasImage && !isNormalSize);
    m_Ui->fitToWindowAct->setChecked(hasImage && isFittedToWindow);
    m_Ui->fitToWindowAct->setEnabled(hasImage && !isFittedToWindow);
    m_Ui->actionFill_to_Window->setChecked(hasImage && isFilledToWindow);
    m_Ui->actionFill_to_Window->setEnabled(hasImage && !isFilledToWindow);
}

void MainWindow::on_actionRemove_Tab_triggered()
{
	removeTab(m_tabwidget->currentIndex());
}

void MainWindow::removeTab(int t)
{
#ifdef QT_DEBUG
    qDebug() << "MainWindow::remove_image("<< t <<")";
#endif
    if (t < 0) return;

    GenericViewer* w = (GenericViewer*)m_tabwidget->widget(t);
    w->blockSignals(true);
    if (w->isHDR())
    {
    	bool doClose = false;

#ifdef QT_DEBUG
    qDebug() << "Remove HDR from MainWindow";
#endif
        if ( w->needsSaving() )
        {
            if ( maybeSave() )
            {
                // if discard OR saved
            	doClose = true;
            }
            // if FALSE, it means that the user said "Cancel"
            // or the saving operation went wrong
            // and we don't need to remove any tab
            else {
                w->blockSignals(false);
            }
        }
        else
        	doClose = true;

        if (doClose) {
			m_tabwidget->removeTab(t);
			w->deleteLater();   // delete yourself whenever you want

			showPreviewPanel(false);
			setWindowModified(false);

			tm_status.is_hdr_ready = false;
			tm_status.curr_tm_frame = NULL;
			tm_status.curr_tm_options = NULL;

            m_tonemapPanel->setEnabled(false);

            m_inputFilesName.clear();
            m_inputExpoTimes.clear();

			m_PreviewscrollArea->hide();
        }
    }
    else
    {
        curr_num_ldr_open--;
        m_tabwidget->removeTab(t);
        w->deleteLater();   // delete yourself whenever you want

        if (curr_num_ldr_open == 1)
            m_Ui->fileSaveAllAction->setEnabled(false);
    }
}

QString MainWindow::getCurrentHDRName()
{
    if (tm_status.is_hdr_ready)
    {
        return tm_status.curr_tm_frame->getFileName();
    }
    else
    {
        return QString(tr("Untitled HDR"));
    }
}

void MainWindow::setMainWindowModified(bool b)
{
    if (b)
    {
        if ( tm_status.is_hdr_ready )
        {
            tm_status.curr_tm_frame->setNeedsSaving(true);
            setWindowModified(true);
        }
    }
    else
    {
        if ( tm_status.is_hdr_ready )
        {
            tm_status.curr_tm_frame->setNeedsSaving(false);
        }
        setWindowModified(false);
    }
}

/*
 * Recent File Handling
 */
void MainWindow::setCurrentFile(const QString &fileName)
{
    QStringList files = luminance_options->value(KEY_RECENT_FILES).toStringList();
    files.removeAll(fileName);
    files.prepend(fileName);
    while (files.size() > MAX_RECENT_FILES)
    {
        files.removeLast();
    }

    luminance_options->setValue(KEY_RECENT_FILES, files);

    // Update ALL MainWindow
    foreach (QWidget *widget, QApplication::topLevelWidgets())
    {
        MainWindow *mainWin = qobject_cast<MainWindow *>(widget);
        if (mainWin)
            mainWin->updateRecentFileActions();
    }
    
    OsIntegration::getInstance().addRecentFile(fileName);
    
}

void MainWindow::updateRecentFileActions()
{
    QStringList files = luminance_options->value(KEY_RECENT_FILES).toStringList();

    int numRecentFiles = qMin(files.size(), (int)MAX_RECENT_FILES);
    separatorRecentFiles->setVisible(numRecentFiles > 0);

    for (int i = 0; i < numRecentFiles; ++i)
    {
        QString text = QString("&%1 %2").arg(i + 1).arg(QFileInfo(files[i]).fileName());
        recentFileActs[i]->setText(text);
        recentFileActs[i]->setData(files[i]);
        recentFileActs[i]->setVisible(true);
    }
    for (int j = numRecentFiles; j < MAX_RECENT_FILES; ++j)
    {
        recentFileActs[j]->setVisible(false);
    }
}

void MainWindow::initRecentFileActions()
{
    separatorRecentFiles = m_Ui->menuFile->addSeparator();

    for (int i = 0; i < MAX_RECENT_FILES; ++i)
    {
        recentFileActs[i] = new QAction(this);
        recentFileActs[i]->setVisible(false);
        m_Ui->menuFile->addAction(recentFileActs[i]);
        connect(recentFileActs[i], SIGNAL(triggered()), this, SLOT(openRecentFile()));
    }
}

void MainWindow::clearRecentFileActions()
{
    for (int i = 0; i < MAX_RECENT_FILES; ++i)
    {
        delete recentFileActs[i];
    }
}

void MainWindow::on_actionFix_Histogram_toggled(bool checked)
{
    if (checked)
    {
        GenericViewer* current = (GenericViewer*) m_tabwidget->currentWidget();
        if ( current==NULL ) return;
        if ( current->isHDR() ) return;

        QScopedPointer<GammaAndLevels> g_n_l( new GammaAndLevels(this, current->getQImage()) );

        m_Ui->actionFix_Histogram->setDisabled(true);

        connect(g_n_l.data(), SIGNAL(updateQImage(QImage)), current, SLOT(setQImage(QImage)));
        int exit_status = g_n_l->exec();

        if ( exit_status == 1 )
        {
#ifdef QT_DEBUG
            qDebug() << "GammaAndLevels accepted!";
#endif
            // pfs::Frame * frame = current->getFrame();
            pfs::gammaAndLevels(current->getFrame(),
                                g_n_l->getBlackPointInput(),
                                g_n_l->getWhitePointInput(),
                                g_n_l->getBlackPointOutput(),
                                g_n_l->getWhitePointOutput(),
                                g_n_l->getGamma());

            // current->setFrame(frame);
        } else {
#ifdef QT_DEBUG
            qDebug() << "GammaAndLevels refused!";
#endif
            current->setQImage(g_n_l->getReferenceQImage());
        }

        m_Ui->actionFix_Histogram->setDisabled(false);
        m_Ui->actionFix_Histogram->setChecked(false);
    }
}

void MainWindow::on_actionWhite_Balance_triggered()
{
    QApplication::setOverrideCursor( QCursor(Qt::WaitCursor) );
    m_Ui->actionWhite_Balance->setEnabled(false);
    m_processingAWB = true;
	m_viewerToProcess = (GenericViewer*) m_tabwidget->currentWidget();
    m_tabwidget->setTabEnabled(m_tabwidget->currentIndex(), false);
    
    Frame *frame = m_viewerToProcess->getFrame();

    m_futureWatcher.setFuture(
                QtConcurrent::run(
                        boost::bind(whiteBalance, boost::ref(*frame), WB_COLORBALANCE)
                    )
                );
}

void MainWindow::whiteBalanceDone()
{
    QApplication::restoreOverrideCursor();
	m_processingAWB = false;
	m_Ui->actionWhite_Balance->setEnabled(true);
    m_viewerToProcess->setEnabled(true);
    m_tabwidget->setTabEnabled(m_tabwidget->indexOf(m_viewerToProcess), true);
    m_tabwidget->setCurrentWidget(m_viewerToProcess);
    m_viewerToProcess->updatePixmap();
    if (m_viewerToProcess->isHDR()) {
        m_viewerToProcess->setNeedsSaving(true);
        m_PreviewPanel->updatePreviews(tm_status.curr_tm_frame->getFrame());
    }
}

void MainWindow::on_actionSoft_Proofing_toggled(bool doProof)
{
	GenericViewer* current = (GenericViewer*) m_tabwidget->currentWidget();
    if ( current == NULL ) return;
	if ( current->isHDR() ) return;
	LdrViewer *viewer = (LdrViewer *) current;
	if (doProof) {
#ifdef QT_DEBUG
		qDebug() << "MainWindow:: do soft proofing";
#endif
		if (m_Ui->actionGamut_Check->isChecked())
			m_Ui->actionGamut_Check->setChecked(false);
		viewer->doSoftProofing(false);
	}
	else {
#ifdef QT_DEBUG
		qDebug() << "MainWindow:: undo soft proofing";
#endif
		viewer->undoSoftProofing();
	}
}

void MainWindow::on_actionGamut_Check_toggled(bool doGamut)
{
	GenericViewer* current = (GenericViewer*) m_tabwidget->currentWidget();
    if ( current == NULL ) return;
	if ( current->isHDR() ) return;
	LdrViewer *viewer = (LdrViewer *) current;
	if (doGamut) {
#ifdef QT_DEBUG
		qDebug() << "MainWindow:: do gamut check";
#endif
		if (m_Ui->actionSoft_Proofing->isChecked())
			m_Ui->actionSoft_Proofing->setChecked(false);
		viewer->doSoftProofing(true);
	}
	else {
#ifdef QT_DEBUG
		qDebug() << "MainWindow:: undo gamut check";
#endif
		viewer->undoSoftProofing();
	}
}

void MainWindow::updateSoftProofing(int i)
{
	QWidget *wgt = m_tabwidget->widget(i);
	GenericViewer *g_v = (GenericViewer *)wgt;

    if (g_v == NULL) return;
    if ( !g_v->isHDR() )
    {
        LdrViewer *l_v = static_cast<LdrViewer*>(g_v);
        if ( !m_Ui->actionSoft_Proofing->isChecked() &&
             !m_Ui->actionGamut_Check->isChecked())
        {
            l_v->undoSoftProofing();
        }
        else if (m_Ui->actionSoft_Proofing->isChecked())
        {
            l_v->doSoftProofing(false);
        }
        else if (m_Ui->actionGamut_Check->isChecked())
        {
            l_v->doSoftProofing(true);
        }
    }
}

void MainWindow::showPreviewsOnTheRight()
{
    m_PreviewscrollArea->setParent(m_centralwidget_splitter);
    luminance_options->setPreviewPanelMode(0);
}

void MainWindow::showPreviewsOnTheBottom()
{
    m_PreviewscrollArea->setParent(m_bottom_splitter);
    luminance_options->setPreviewPanelMode(1);
}

void MainWindow::on_actionFits_Importer_triggered()
{
#ifdef HAVE_CFITSIO
    FitsImporter importer;

    if (importer.exec() == QDialog::Accepted)
    {
        pfs::Frame *frame = importer.getFrame();
        emit load_success(frame, tr("FITS Image"), QStringList(), true);
    }
#endif
}

void MainWindow::on_actionShow_Full_Screen_toggled(bool b)
{
    b ? this->showFullScreen() : this->showMaximized();
}

void MainWindow::on_actionShow_Image_Full_Screen_triggered()
{
    GenericViewer* g_v = (GenericViewer*)m_tabwidget->currentWidget();
    bool isHdr = g_v ? g_v->isHDR() : false;
    pfs::Frame* frame = pfs::copy(g_v->getFrame()); //frame is assigned to one viewer that will take care of deleting it
    if (isHdr)
    {
        m_fullScreenHdrViewer->setFrame(frame);
        m_fullScreenHdrViewer->showFullScreen();
    }
    else
    {
        m_fullScreenLdrViewer->setFrame(frame);
        m_fullScreenLdrViewer->showFullScreen();
    }
}
