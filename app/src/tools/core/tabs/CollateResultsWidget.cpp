/*
 * Copyright (C) 2007-2013 Dyson Technology Ltd, all rights reserved.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "CollateResultsWidget.h"

#include "ui_CollateResultsWidget.h"

#include "UnknownLengthProgressDlg.h"

#include "CoverageMetrics.h"

#include "Collection.h"
#include "RunsCollection.h"
#include "RoomsCollection.h"
#include "RunSchema.h"

#include "WbConfigTools.h"
#include "WbDefaultKeys.h"

#include "OpenCvTools.h"

#include "FileUtilities.h"
#include "FileDialogs.h"
#include "Message.h"

#include "Logging.h"

#include <opencv/cv.h>
#include <opencv/highgui.h>

#include <QtCore/QDir>
#include <QtGui/QMessageBox>
#include <QFileDialog>
#include <QApplication>
#include <QStandardItemModel>
#include <QStandardItem>
#include <QTemporaryFile>
#include <QDirIterator>
#include <QTextStream>

#include <fstream>

#ifdef _WIN32
#include "windows.h"
#else
#define MAX_PATH 255
#endif

CollateResultsWidget::CollateResultsWidget( QWidget* parent ) :
    Tool( parent, CreateSchema() ),
    m_ui( new Ui::CollateResultsWidget )
{
    m_ui->setupUi( this );

    QObject::connect( m_ui->m_loadBtn,
                      SIGNAL( clicked() ),
                      this,
                      SLOT( LoadResultsButtonClicked() ) );
    QObject::connect( m_ui->m_analyseBtn,
                      SIGNAL( clicked() ),
                      this,
                      SLOT( AnalyseResultsButtonClicked() ) );
    QObject::connect( m_ui->m_browsePlanBtn,
                      SIGNAL( clicked() ),
                      this,
                      SLOT( BrowseForFloorPlanClicked() ) );
    QObject::connect( m_ui->m_browseMaskBtn,
                      SIGNAL( clicked() ),
                      this,
                      SLOT( BrowseForFloorMaskClicked() ) );

    AddMapper( KeyName( "floorPlanName" ), m_ui->m_floorPlanFileNameEdit );
    AddMapper( KeyName( "floorMaskName" ), m_ui->m_floorMaskFileNameEdit );
}

CollateResultsWidget::~CollateResultsWidget()
{
    delete m_ui;
}

const QString CollateResultsWidget::GetSubSchemaDefaultFileName() const
{
    return "collateResults.xml";
}

bool CollateResultsWidget::CanClose() const
{
    return true;
}

const QString CollateResultsWidget::CannotCloseReason() const
{
    return tr("Please complete data before leaving tab.");
}

const WbSchema CollateResultsWidget::CreateSchema()
{
    WbSchema schema( CreateWorkbenchSubSchema( KeyName( "collateResults" ),
                                               tr( "Collate Results" ) ) );

    schema.AddSingleValueKey( KeyName( "floorPlanName" ),
                              WbSchemaElement::Multiplicity::One );
    schema.AddSingleValueKey( KeyName( "floorMaskName" ),
                              WbSchemaElement::Multiplicity::One );

    return schema;
}

void CollateResultsWidget::BrowseForFloorPlanClicked()
{
    FileDialogs::ExtendedFileDialog fileDialog( this,
                                                tr( "Select Image File" ),
                                                GetCurrentConfig().GetAbsoluteFileInfo().absolutePath(),
                                                "Images( *.png *.jpg *.bmp *.ppm );;All Files( * )",
                                                true );
    const int result = fileDialog.exec();
    if ( result == QFileDialog::Accepted )
    {
        QString floorPlanName( fileDialog.selectedFiles().front() );

        if ( !floorPlanName.isEmpty() )
        {
            m_ui->m_floorPlanFileNameEdit->setText( floorPlanName );
        }
    }
}

void CollateResultsWidget::BrowseForFloorMaskClicked()
{
    FileDialogs::ExtendedFileDialog fileDialog( this,
                                                tr( "Select Image File" ),
                                                GetCurrentConfig().GetAbsoluteFileInfo().absolutePath(),
                                                "Images( *.png *.jpg *.bmp *.ppm );;All Files( * )",
                                                true );
    const int result = fileDialog.exec();
    if ( result == QFileDialog::Accepted )
    {
        QString floorMaskName( fileDialog.selectedFiles().front() );

        if ( !floorMaskName.isEmpty() )
        {
            m_ui->m_floorMaskFileNameEdit->setText( floorMaskName );
        }
    }
}

#define TABLE_COL_USE  0
#define TABLE_COL_RUN  1
#define TABLE_COL_ROOM 2

void CollateResultsWidget::LoadResultsButtonClicked()
{
    tableModel = new QStandardItemModel();

    tableModel->setHorizontalHeaderItem(TABLE_COL_USE, new QStandardItem(QString("")));
    tableModel->setHorizontalHeaderItem(TABLE_COL_RUN, new QStandardItem(QString("Run")));
    tableModel->setHorizontalHeaderItem(TABLE_COL_ROOM, new QStandardItem(QString("Room")));

    Collection runsCollection = RunsCollection();
    Collection roomsCollection = RoomsCollection();
    runsCollection.SetConfig( GetCurrentConfig() );
    roomsCollection.SetConfig( GetCurrentConfig() );

    for (int n = 0; n < (int)runsCollection.NumElements(); ++n)
    {
        const WbConfig runConfig = runsCollection.ElementAt( n ).value;

        const KeyId roomId = runConfig.GetKeyValue( RunSchema::roomIdKey ).ToKeyId();

        const WbConfig roomConfig = roomsCollection.ElementById( roomId );

        const QString runName = runConfig.GetKeyValue( WbDefaultKeys::displayNameKey ).ToQString();
        const QString roomName = roomConfig.GetKeyValue( WbDefaultKeys::displayNameKey ).ToQString();

        // Create check box item...
        QStandardItem* item = new QStandardItem(true);
        item->setCheckable(true);
        item->setCheckState(Qt::Unchecked);
        tableModel->setItem(n, TABLE_COL_USE, item);

        // Create text item(s)...
        tableModel->setItem(n, TABLE_COL_RUN, new QStandardItem(runName));
        tableModel->setItem(n, TABLE_COL_ROOM, new QStandardItem(roomName));
    }

    // resize header column
    m_ui->m_runsTable->horizontalHeader()->setResizeMode(QHeaderView::ResizeToContents);
    // Set model...
    m_ui->m_runsTable->setModel( tableModel );

    m_ui->m_analyseBtn->setEnabled(true);
}

void CollateResultsWidget::AnalyseResultsButtonClicked()
{
    const WbConfig& config = GetCurrentConfig();

    LOG_TRACE("Analysing Results...");

    bool successful = true;

    QString selectedRoom;

    // Check all selected runs have the same room
    // then get the floor plan and floor mask for
    // that room to provide to the analysis...

    for (int n = 0; n < tableModel->rowCount() && successful; ++n)
    {
        QStandardItem* itemCheck = tableModel->item(n, TABLE_COL_USE);
        QStandardItem* itemRoom = tableModel->item(n, TABLE_COL_ROOM);

        QString room = itemRoom->data().toString();

        if ( itemCheck->checkState() == Qt::Checked )
        {
            if ( selectedRoom.isEmpty() )
            {
                selectedRoom = room;
            }

            if ( room != selectedRoom )
            {
                successful = false;
            }
        }
    }

    if (successful)
    {
        QTemporaryFile tmpFile( QDir::tempPath() + "/files.txt");

        successful = tmpFile.open();

        if (successful)
        {
            QTextStream fileStrm( &tmpFile );

            Collection runsCollection = RunsCollection();
            runsCollection.SetConfig( config );

            for (int n = 0; n < tableModel->rowCount(); ++n)
            {
                QStandardItem* item = tableModel->item(n, TABLE_COL_USE);

                if ( item->checkState() == Qt::Checked )
                {
                    QFileInfo runDir = runsCollection.ElementAt( n ).value
                                                   .GetAbsoluteFileInfo();

                    QDirIterator dirIterator(runDir.absolutePath(),
                                             QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot,
                                             QDirIterator::Subdirectories);

                    while (dirIterator.hasNext())
                    {
                        dirIterator.next();

                        if (dirIterator.fileInfo().isDir())
                        {
                            foreach( QFileInfo fileInfo, QDir(dirIterator.filePath()).entryInfoList( QStringList("coverage_overlay.png"),
                                                                                                     QDir::Files | QDir::NoDotAndDotDot ) )
                            {
                                fileStrm << fileInfo.filePath() << "\n";
                            }
                        }
                    }
                }
            }

            tmpFile.close();

            if (CreateAnalysisResultDirectory( config ))
            {
                const QString floorPlanName(
                    config.GetKeyValue( KeyName( "floorPlanName" ) ).ToQString() );
                const QString floorMaskName(
                    config.GetKeyValue( KeyName( "floorMaskName" ) ).ToQString() );

                const QString totalCoverageCsvName(
                    config.GetAbsoluteFileNameFor( "results/total_coverage.csv" ) );
                const QString totalCoverageImgName(
                    config.GetAbsoluteFileNameFor( "results/total_coverage.png" ) );

                if ( successful )
                {
                    UnknownLengthProgressDlg* const progressDialog = new UnknownLengthProgressDlg( this );
                    progressDialog->Start( tr( "Working..." ), tr( "" ) );

                    ExitStatus::Flags exitCode = AnalyseResults( floorPlanName.toAscii().data(),        // floorPlanName
                                                                 floorMaskName.toAscii().data(),        // floorMaskName
                                                                 tmpFile.fileName().toAscii().data(),   // inputFileList
                                                                 totalCoverageCsvName.toAscii().data(),
                                                                 totalCoverageImgName.toAscii().data() );

                    successful = ( exitCode == ExitStatus::OK_TO_CONTINUE );

                    if ( successful )
                    {
                        progressDialog->Complete( tr( "Results Analysis Successful" ),
                                                  tr( "Results have been computed." ) );
                    }
                    else
                    {
                        progressDialog->ForceClose();

                        Message::Show( 0,
                                       tr( "Results Analysis Failed" ),
                                       tr( "See the log for details!" ),
                                       Message::Severity_Critical );
                    }
                }
            }
        }
	    else
        {
		    QMessageBox::critical(this,
                                  tr( "Results Analysis Failed" ),
                                  tr( "Temporary file missing!" ));
        }
    }
    else
    {
        QMessageBox::critical(this,
                              tr( "Results Analysis Failed" ),
                              tr( "Runs must common room!" ));
    }
}

// ----------------------------------------------------------------------------------------------------------------------------
// ----------------------------------------------------------------------------------------------------------------------------

const ExitStatus::Flags CollateResultsWidget::AnalyseResults( char* floorPlanName,
                                                              char* floorMaskName,         // -flr
                                                              char* overlayListFileName,   // -files
                                                              char* totalCoverageCsvName,
                                                              char* totalCoverageImgName ) // -out
{
    ExitStatus::Flags exitStatus = ExitStatus::OK_TO_CONTINUE;

    IplImage* floorMaskImg = NULL;

    if ( floorMaskName )
    {
        floorMaskImg = OpenCvTools::LoadSingleChannelImage( floorMaskName );
    }

    if ( !floorMaskImg )
    {
        LOG_ERROR("Could not load floor mask image!");

        return ExitStatus::ERRORS_OCCURRED;
    }

    IplImage* floorPlanImg = NULL;

    if ( floorPlanName )
    {
        floorPlanImg = cvLoadImage( floorPlanName );
    }

    if ( !floorPlanImg )
    {
        LOG_ERROR("Could not load floor plan image!");

        cvReleaseImage( &floorMaskImg );

        return ExitStatus::ERRORS_OCCURRED;
    }
    else if ((floorPlanImg->width != floorMaskImg->width) ||
             (floorPlanImg->height != floorMaskImg->height))
    {
        LOG_ERROR("Floor plan and mask sizes differ!");

        cvReleaseImage( &floorMaskImg );
        cvReleaseImage( &floorPlanImg );

        return ExitStatus::ERRORS_OCCURRED;
    }

    std::vector<std::string> fileNames;

    FILE* f = fopen( overlayListFileName, "r" );

    while (f && !feof(f))
    {
        char fileName[MAX_PATH];
        if ( fgets( fileName, sizeof(fileName), f ) )
        {
            // Strip trailing newline
            const size_t len = strlen( fileName );
            fileName[len-1] = '\0';
            fileNames.push_back(fileName);
        }
    }

    if ( fileNames.size() == 0 )
    {
        LOG_WARN("No coverage images supplied!");
    }

    // Number of non-zero pixels in floor mask is total number of
    // pixels of interest - the size of the area we are looking at.
    const int nTotalPixels = floorMaskImg->width * floorMaskImg->height;
    const int nFloorPixels = cvCountNonZero( floorMaskImg );

    LOG_INFO(QObject::tr("Total pixels (WxH) = %1.").arg(nTotalPixels));
    LOG_INFO(QObject::tr("Total floor pixels = %2.").arg(nFloorPixels));

    // Overlay floor mask onto floor image...
    OpenCvTools::DrawColouredOverlay( floorPlanImg,
                                      floorMaskImg,
                                      CV_RGB(100,0,0),
                                      std::bind2nd(std::equal_to<int>(), 255) );

    // Keep track of total coverage counts in a separate map...
    IplImage* totalCoverageImg = cvCreateImage( cvSize( floorMaskImg->width,
                                                        floorMaskImg->height), IPL_DEPTH_8U, 1 );
    cvZero( totalCoverageImg );

    FILE* fp = fopen( totalCoverageCsvName, "w" );

    CoverageMetrics::PrintCsvHeaders(fp);

    int run = 0;
    for (std::vector<std::string>::const_iterator i = fileNames.begin(); i != fileNames.end(); ++i)
    {
        LOG_INFO(QObject::tr("Run: %1 (file: %2).").arg(++run)
                                                   .arg(i->c_str()));

        // Load the coverage mask, limit it to the floor mask area...
        IplImage* coverageMaskImg = OpenCvTools::LoadSingleChannelImage( i->c_str() );

        if ((coverageMaskImg->height != floorMaskImg->height) ||
            (coverageMaskImg->width != floorMaskImg->width))
        {
            LOG_ERROR(QObject::tr("Coverage image (%1) and floor mask sizes differ!").arg(i->c_str()));

            cvReleaseImage( &coverageMaskImg );
            continue;
        }

        // Constrain coverage to the floor mask area...
        cvAnd( coverageMaskImg, floorMaskImg, coverageMaskImg );

        // Add this coverage to the total...
        cvAdd( coverageMaskImg, totalCoverageImg, totalCoverageImg );

        CoverageMetrics::PrintCsvLineForPass(fp, run, totalCoverageImg, nFloorPixels);

        // Clean up...
        cvReleaseImage( &coverageMaskImg );
    }

    // Update floor plan image with total coverage.

    for (int level = 1; level < RunEntry::MAX_LEVEL; ++level)
    {
        OpenCvTools::DrawColouredOverlay( floorPlanImg,
                                          totalCoverageImg,
                                          CV_RGB(0,level*40,0),
                                          std::bind2nd(std::equal_to<int>(), level) );
    }

    OpenCvTools::DrawColouredOverlay( floorPlanImg,
                                      totalCoverageImg,
                                      CV_RGB(0,255,0),
                                      std::bind2nd(std::greater_equal<int>(), RunEntry::MAX_LEVEL) );

    // Clean up...
    if ( f ) { fclose( f ); }
    if ( fp ) { fclose( fp ); }

    cvSaveImage( totalCoverageImgName, floorPlanImg );

    cvReleaseImage( &floorMaskImg );
    cvReleaseImage( &floorPlanImg );

    LOG_TRACE("Finished.");

    return exitStatus;
}

bool CollateResultsWidget::CreateAnalysisResultDirectory(const WbConfig& config)
{
    QDir m_resultDir( config.GetAbsoluteFileNameFor( "results" ) );

    const QString resultDirName = m_resultDir.dirName();

    QDir resultDirParent( m_resultDir );

    if ( !resultDirParent.cdUp() )
    {
        Message::Show( this,
                       tr( "Track Results Analysis" ),
                       tr( "Error - Save Workbench!" ),
                       Message::Severity_Critical );
        return false;
    }

    if (resultDirParent.exists( resultDirName ))
    {
        QMessageBox mb;
        mb.setText(QObject::tr("Track Results Analysis"));
        mb.setInformativeText(QObject::tr( "Query - Overwrite data?"));
        mb.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
        int ret = mb.exec();

        if (ret == QMessageBox::Cancel)
        {
            return false;
        }

        FileUtilities::DeleteDirectory( resultDirParent.absoluteFilePath(resultDirName) );
    }

    if ( !resultDirParent.mkdir( resultDirName ) || !resultDirParent.cd( resultDirName ))
    {
        Message::Show( this,
                       tr( "Track Results Analysis" ),
                       tr( "Error - Missing folder!" ),
                       Message::Severity_Critical );
        return false;
    }

    return true;
}
