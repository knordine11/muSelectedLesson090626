#include "widget.h"
#include "ui_widget.h"
#include "fileloader.h"
#include <QAudioDevice>
#include <QAudioSource>
#include <QAudioOutput>
#include <QMediaDevices>
#include <QtEndian>
#include "fftstuff.h"
#include <qtimer.h>
#include <math.h>
#include <stdlib.h>
#include <iostream>
#include <QMessageBox>
#include <QUrl>
#include <QTimer>

extern bool collectMicData;
extern double rec_arr[];    // DEFINED AS DOUBLE FOR FFTW
extern int rec_arr_cnt;
extern int frame_start;
extern int frame_size;
extern int frame_end;
extern int rec_arr_end;
extern QString currentlesson;
extern QList<QString> gNote;
extern QList<QString> gKey;
extern QList<QString> gTestGroup;
extern QList<QByteArray> rawRecArrays;
extern QList<int> testNotes;
const QList <QString> note_letters = {"C", "C#", "D", "D#", "E",
                                     "F", "F#", "G", "G#", "A", "A#", "B" };
int curLessonInt;
int listIndex;
QMap<QString, int> tonic_map = {
    {"G3", 43}, {"A3", 45}, {"B3", 47}, {"C4", 48}, {"D4", 50}, {"A4", 52}, {"B4", 54}, {"C5", 55}
};
QMap<int, QString> tilesGoodBadMap = {
    {0, "note0"}, {1, "note2"}, {2, "note4"}, {3, "note5"}, {4, "note7"}, {5, "note9"},
    {6, "note11"}, {7, "note12"}
};
QMap<int, int> tileKbShift = {
    {0, 0}, {1, 2}, {2, 4}, {3, 5}, {4, 7}, {5, 9}, {6, 11}, {7, 12}
};

QList<int> kbNotePlayLists;

FftStuff ftw;
int accValue = 0;
int displayDuration = 3000;
int playDuration = 3000;
QString lessonData = "";
int scoreTotals = 0;

Microphone::Microphone(const QAudioFormat &format) : m_format(format) {
    qDebug()<<" YOU SHOULD SEE THIS ";
}

Microphone::~Microphone()
{

}

void Microphone::start()
{
    open(QIODevice::WriteOnly);
}

void Microphone::stop()
{
    close();
}

qint64 Microphone::readData(char * /* data */, qint64 /* maxlen */)      // NOT USED IN EXAMPLE
{
    return 0;
}

qreal Microphone::getNoteValue(const char *data, qint64 len) const
{
    const int channelBytes = m_format.bytesPerSample();
    const int sampleBytes = m_format.bytesPerFrame();
    const int numSamples = len / sampleBytes;

    float maxValue = 0;
    auto *ptr = reinterpret_cast<const unsigned char *>(data);

    for (int i = 0; i < numSamples; ++i) {
        float value = m_format.normalizedSampleValue(ptr);
        rec_arr[rec_arr_cnt]=value;
        //maxValue = qMax(value, maxValue);
        ptr += channelBytes;
        rec_arr_cnt++;
    };
    if(rec_arr_cnt > frame_end){
        cout<<"\n  NEXT FRAME $$$$ "<<frame_start<<endl;

        ftw.DoIt(frame_start, frame_size);
        frame_start = frame_end;
        frame_end = frame_end + frame_size;}
    if (rec_arr_cnt > 200000)
    {
        qDebug() <<"                      restart here";
        rec_arr_cnt = 0;
        qDebug() <<"                      post restart here";
        return 0;
    }
    return maxValue;
}

qint64 Microphone::writeData(const char *data, qint64 len)
{
    // qDebug() << "enter writeData" << rec_arr_cnt;
    m_level = getNoteValue(data, len);
    return len;
}

Speaker::Speaker()
{

}

void Speaker::start()
{
    open(QIODevice::ReadOnly);
}

void Speaker::stop()
{
    m_pos = 0;
    close();
}

void Speaker::newTest(QByteArray bufferOut)
{
    qDebug()<<"  %%%%  NEW TEST ";
    m_buffer.assign(bufferOut);
    m_buffer.clear();
    qDebug()<<&m_buffer<<" m_buffer_.size() " << m_buffer.size();
    m_buffer = bufferOut;
    qDebug()<<&m_buffer<<" m_buffer_.size() " << m_buffer.size();
}

void Speaker::clearBuffer()
{
    QByteArray emptyArray = {};
    m_buffer.assign(emptyArray);
    m_buffer.clear();
    m_buffer = emptyArray;
}



qint64 Speaker::readData(char *data, qint64 len)
{
    qDebug() << "enter speaker readdata...is on main? " << QThread::isMainThread();
    qint64 total = 0;
    if (!m_buffer.isEmpty()) {
        while (len - total > 0) {
            const qint64 chunk = qMin((m_buffer.size() - m_pos), len - total);
            memcpy(data + total, m_buffer.constData() + m_pos, chunk);
            m_pos = (m_pos + chunk) % m_buffer.size();
            total += chunk;
            qDebug() << "chunk..." << chunk << "pos> = " << m_pos ;
        }
    }
    return total;
}

qint64 Speaker::writeData(const char *data, qint64 len)
{
    Q_UNUSED(data);
    Q_UNUSED(len);

    return 0;
}

qint64 Speaker::bytesAvailable() const
{
    return QIODevice::bytesAvailable();
}

Widget::Widget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Widget)
{
    ui->setupUi(this);
    this->setWindowTitle("Ear Trainer version 1.0");
    this->setWindowIcon(QIcon(":/img/note.png"));
    const QAudioDevice &defaultDeviceInfo = QMediaDevices::defaultAudioInput();
    initializeAudioInput(QMediaDevices::defaultAudioInput());
    initializeAudioOutput(m_devicesOut->defaultAudioOutput());
    QPixmap pix(":/img/down-arrow.png");
    ui->lb_arrow->setPixmap(pix);
    ui->lb_arrow->move(800, 100);
    FileLoader::ReadConfig();
    FileLoader::ReadLesson();
    ui->btnStart->setVisible(false);
    qDebug() << "gTestGroup = " << gTestGroup;
    qDebug() << "gNote = " << gNote;
    // load cboLessons
    for(int i = 0; i < 12; i++)
    {
        QString temp = "Key " + gNote[i] + " Notes " + gTestGroup[i];
        ui->cboLessonSelect->addItem(temp);
    }
    m_timer = new QTimer(this);
    m_Microphone->moveToThread(&MicThread);
    MicThread.setObjectName("MicThread");
    MicThread.start();
    m_Speaker->moveToThread(&SpeakerThread);
    connect(&ftw, &FftStuff::valueChanged,this, &Widget::updateKBnote, Qt::QueuedConnection);
    connect(&ftw, &FftStuff::on_foundNote,this, &Widget::Got_Note, Qt::QueuedConnection);
    connect(m_timer,&QTimer::timeout,this,&Widget::TimeOut);
}

Widget::~Widget()
{
    MicThread.terminate();
    SpeakerThread.terminate();
    delete m_audioSource;
    delete m_Microphone;
    delete ui;
}

void Widget::initializeAudioInput(const QAudioDevice &deviceInfo)
{
    QAudioFormat format;
    format.setSampleRate(44100);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);
    m_Microphone = (new Microphone(format));
    m_audioSource = (new QAudioSource(deviceInfo, format));
    qDebug() <<  "buffer size: " << m_audioSource->bufferSize();
    m_Microphone->start();
}

void Widget::initializeAudioOutput(const QAudioDevice &deviceInfo)
{
    qDebug() << "initializeAudioOutput...";
    QAudioFormat format;
    format.setSampleRate(44100);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);
    qDebug() << "     !!!   from  INIT format: " << format.sampleRate();
    m_Speaker.reset(new Speaker());
    m_audioOutput.reset(new QAudioSink(deviceInfo, format));
}

void buildkbNotePlayList(int tonicNote)
{
    //QList<int> kbNotePlayLists;
    int kbNoteFileName = tonicNote;
    kbNotePlayLists.append(kbNoteFileName);
    kbNoteFileName = tonicNote + 2;
    kbNotePlayLists.append(kbNoteFileName);
    kbNoteFileName = tonicNote + 4;
    kbNotePlayLists.append(kbNoteFileName);
    kbNoteFileName = tonicNote + 5;
    kbNotePlayLists.append(kbNoteFileName);
    kbNoteFileName = tonicNote + 7;
    kbNotePlayLists.append(kbNoteFileName);
    kbNoteFileName = tonicNote + 9;
    kbNotePlayLists.append(kbNoteFileName);
    kbNoteFileName = tonicNote + 11;
    kbNotePlayLists.append(kbNoteFileName);
    kbNoteFileName = tonicNote + 12;
    kbNotePlayLists.append(kbNoteFileName);
    qDebug() << kbNotePlayLists;
}

void Widget::paintEvent(QPaintEvent * /* event */)
{
    QPixmap pix(100,60);
    pix.fill(Qt::white);
    //create a QPainter and pass a pointer to the device.
    QPainter *painter = new QPainter(&pix);
    QPen outsidePen(Qt::red, 4, Qt::SolidLine);
    painter->setPen(outsidePen);
    painter->drawEllipse(35, 15, 30, 30);
    QPen insidePen(Qt::green, 4, Qt::SolidLine);
    painter->setPen(insidePen);
    painter->drawEllipse(40 + accValue, 20, 20, 20);
    QPen linePen(Qt::black, 1, Qt::SolidLine);
    painter->setPen(linePen);
    painter->drawLine(0, 30, 100, 30);
    painter->drawLine(50, 0, 50, 60);
    painter->end();
    ui->lb_tuner->setPixmap(pix);
}

void Widget::on_btnStart_clicked()
{
    MicThread.start();

    qDebug() << "start pushed...";
    ui->btnStart->setVisible(false);
    qDebug() << "starting...";
    // get sound array set
    m_Speaker->clearBuffer();
    tonicNote = tonic_map[gNote[listIndex]];
    qDebug() << tonicNote;
    qDebug() << gNote[listIndex];
    FileLoader files;
    files.GetFileList(tonicNote);
    buildkbNotePlayList(tonicNote);
    FileLoader::GetRandomTestSet(gTestGroup[listIndex]);
    qDebug() << gTestGroup[listIndex];
    playedCnt = 0;
    goodCnt = 0;
    nPos = 0;
    restartAudioStream();
    do_Lesson(nPos);
    nPos++;
}

void Widget::restartAudioStream()
{
    m_audioSource->stop();
    qDebug()<< "is main Thread: " << QThread::isMainThread();
    m_audioSource->start(m_Microphone);
    qDebug() << "============================ restartAudioStream====>>>";
}

void Widget::do_Lesson(int nPos)
{
    if(nPos == 0) lessonData = lessonData + "\n\nLesson "
                     + QString::number(curLessonInt + 1)
                     + " Test Notes " + gTestGroup[listIndex] + "\n";
    m_timer->setInterval(playDuration);
    m_timer->start();
    playedNote = testNotes[nPos] - 1;
    kbPlayed = kbNotePlayLists[testNotes[nPos] - 1];
    m_Speaker->newTest( rawRecArrays[testNotes[nPos] - 1]);
    qDebug() << "testNotes value: " << testNotes[nPos] - 1;
    m_Speaker->stop();
    m_Speaker->start();
    m_audioOutput->stop();
    m_audioOutput->start(m_Speaker.data());
    SpeakerThread.start();
}

void Widget::updateKBnote(int kbValue, float acc)
{
    int letter = kbValue%12;
    int octaveValue = kbValue/12;
    QString theNote = note_letters[letter] + QString::number(octaveValue);
    qDebug() << "knNote... " << theNote;
    QString alphaNote = note_letters[letter] + QString::number(octaveValue);
    qDebug() << "alphaNote... " << alphaNote;
    ui->lb_note->setText(alphaNote);
    qDebug() << "acc is: " << (int)(acc * 1000);
    accValue = (int)(acc * 1000);
    ui->lb_tuner->repaint();
}

void Widget::TimeOut()
{
    m_timer->stop();
    m_audioSource->suspend();
    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, "Timed Out", "Continue?",
                                  QMessageBox::Yes|QMessageBox::No);
    if (reply == QMessageBox::Yes) {
        qDebug() << "continuing.";
        timeoutRetry();
    } else {
        qDebug() << "No was clicked";
        QApplication::quit();
        qDebug() << "app quit called...";
    }
}

void Widget::timeoutRetry()
{
    qDebug() << "continuing...>>>";
    m_audioSource->resume();
    for(int i = 0; i < 200000; i++)
    {
        rec_arr[i] = 0;
    }
    rec_arr_cnt = 0;
    frame_start = 0;
    frame_end = 2048;
    m_Microphone->reset();
    m_audioSource->reset();
    restartAudioStream();
    QThread::msleep(100);
    nPos--;
    do_Lesson(nPos);
    nPos++;
}

void Widget::Got_Note(int kbValue)
{
    m_timer->stop();
    playedCnt++;
    m_audioSource->stop();
    ui->txtPlayed->setText(QString::number(playedCnt));
    if (kbValue == kbPlayed)
    {
        goodCnt++;
        ui->txtGood->setText(QString::number(goodCnt));
    }    
    qDebug() << "-->note found: " << kbValue;
    int heardNote = kbValue;
    int floatingNoteValue = kbValue%12;
    qDebug() << "Floating value = " << floatingNoteValue;
    if(heardNote < tonicNote){
        ui->lb_Octave->setText("-1 oct");
        heardNote += 12;
        if(heardNote < tonicNote){
            ui->lb_Octave->setText("-2 oct");
            heardNote += 12;
        }
    }
    if(heardNote > tonicNote + 12){
        ui->lb_Octave->setText("+1 oct");
        heardNote -= 12;
    }
    int octOffBy = 55+(heardNote - tonicNote)*45;
    ui->lb_Octave->move(octOffBy, 220);
    ui->lb_arrow->move(55+((heardNote - tonicNote)*45), 250);
    ui->lb_arrow->repaint();
    kbPlayedNote = tonicNote + tileKbShift[playedNote];
    qDebug() << "-->heardNote: " << heardNote << " = " << kbPlayedNote;

    lessonData = lessonData + "Played Note = " + QString::number(kbPlayedNote) + "  Heard Note = " +QString::number(heardNote) + "\n";
    switch (playedNote){
    case 0:
        if (kbPlayedNote == heardNote)
        {
            ui->note0->setStyleSheet("QLabel { background-color: green;"
                                     "border-style: outset;"
                                     " border-width: 3px; border-color: black;}");
        } else {
            ui->note0->setStyleSheet("QLabel { background-color: red;"
                                     "border-style: outset;"
                                     " border-width: 3px; border-color: black;}");
        }
        break;
    case 1:
        if (kbPlayedNote == heardNote)
        {
            ui->note1->setStyleSheet("QLabel { background-color: green;"
                                     "border-style: outset;"
                                     " border-width: 3px; border-color: black;}");
        } else {
            ui->note1->setStyleSheet("QLabel { background-color: red;"
                                     "border-style: outset;"
                                     " border-width: 3px; border-color: black;}");
        }
        break;
    case 2:
        if (kbPlayedNote == heardNote)
        {
            ui->note2->setStyleSheet("QLabel { background-color: green;"
                                     "border-style: outset;"
                                     " border-width: 3px; border-color: black;}");
        } else {
            ui->note2->setStyleSheet("QLabel { background-color: red;"
                                     "border-style: outset;"
                                     " border-width: 3px; border-color: black;}");
        }
        break;
    case 3:
        if (kbPlayedNote == heardNote)
        {
            ui->note3->setStyleSheet("QLabel { background-color: green;"
                                     "border-style: outset;"
                                     " border-width: 3px; border-color: black;}");
        } else {
            ui->note3->setStyleSheet("QLabel { background-color: red;"
                                     "border-style: outset;"
                                     " border-width: 3px; border-color: black;}");
        }
        break;
    case 4:
        if (kbPlayedNote == heardNote)
        {
            ui->note4->setStyleSheet("QLabel { background-color: green;"
                                     "border-style: outset;"
                                     " border-width: 3px; border-color: black;}");
        } else {
            ui->note4->setStyleSheet("QLabel { background-color: red;"
                                     "border-style: outset;"
                                     " border-width: 3px; border-color: black;}");
        }
        break;
    case 5:
        if (kbPlayedNote == heardNote)
        {
            ui->note5->setStyleSheet("QLabel { background-color: green;"
                                     "border-style: outset;"
                                     " border-width: 3px; border-color: black;}");
        } else {
            ui->note5->setStyleSheet("QLabel { background-color: red;"
                                     "border-style: outset;"
                                     " border-width: 3px; border-color: black;}");
        }
        break;
    case 6:
        if (kbPlayedNote == heardNote)
        {
            ui->note6->setStyleSheet("QLabel { background-color: green;"
                                     "border-style: outset;"
                                     " border-width: 3px; border-color: black;}");
        } else {
            ui->note6->setStyleSheet("QLabel { background-color: red;"
                                     "border-style: outset;"
                                     " border-width: 3px; border-color: black;}");
        }
        break;
    case 7:
        if (kbPlayedNote == heardNote)
        {
            ui->note7->setStyleSheet("QLabel { background-color: green;"
                                     "border-style: outset;"
                                     " border-width: 3px; border-color: black;}");
        } else {
            ui->note7->setStyleSheet("QLabel { background-color: red;"
                                     "border-style: outset;"
                                     " border-width: 3px; border-color: black;}");
        }
    }
    ui->note0->repaint();
    QThread::currentThread()->msleep(displayDuration);
    ui->note0->setStyleSheet("QLabel { background-color: white;"
                             "border-style: outset;"
                             " border-width: 3px; border-color: black;}");
    ui->note1->setStyleSheet("QLabel { background-color: white;"
                             "border-style: outset;"
                             " border-width: 3px; border-color: black;}");
    ui->note2->setStyleSheet("QLabel { background-color: white;"
                             "border-style: outset;"
                             " border-width: 3px; border-color: black;}");
    ui->note3->setStyleSheet("QLabel { background-color: white;"
                             "border-style: outset;"
                             " border-width: 3px; border-color: black;}");
    ui->note4->setStyleSheet("QLabel { background-color: white;"
                             "border-style: outset;"
                             " border-width: 3px; border-color: black;}");
    ui->note5->setStyleSheet("QLabel { background-color: white;"
                             "border-style: outset;"
                             " border-width: 3px; border-color: black;}");
    ui->note6->setStyleSheet("QLabel { background-color: white;"
                             "border-style: outset;"
                             " border-width: 3px; border-color: black;}");
    ui->note7->setStyleSheet("QLabel { background-color: white;"
                             "border-style: outset;"
                             " border-width: 3px; border-color: black;}");

    // m_Microphone->start();
    ui->lb_Octave->setText("");
    ui->lb_arrow->move(800, 100);
    qDebug() << "Keyboard value heard: " << kbValue;
    if(nPos == 20){
        ui->btnStart->setVisible(true);
        MicThread.quit();
        SpeakerThread.quit();
        kbNotePlayLists.clear();
        noteFiles.clear();
        testNotes.clear();
        rawRecArrays.clear();
        return;
    }
    else
    {
        play_next_note();
    }

}

void Widget::play_next_note()
{
    qDebug() << "next pressed";
    qDebug() << "SpeakerThread running: " << SpeakerThread.isRunning();
    qDebug() << "micThread running: " << MicThread.isRunning();
    qDebug() << "m_Microphone is open: " << m_Microphone->isOpen();

    rec_arr_cnt = 0;
    frame_start = 0;
    frame_end = 2048;
    m_Microphone->reset();
    m_audioSource->reset();
    restartAudioStream();
    // zero out rec_arr with each mic get
    for(int i = 0; i < 200000; i++)
    {
        rec_arr[i] = 0;
    }
    do_Lesson(nPos);
    nPos++;
}

void Widget::on_cboLessonSelect_currentIndexChanged(int index)
{
    qDebug() << "Index = " << index;
    listIndex = index - 1;
    currentlesson = QString::number(listIndex + 1);
    FileLoader::GetRandomTestSet(gTestGroup[listIndex]);
    qDebug() << "random = " << gTestGroup[listIndex];
    ui->txtLessonNumber->setText(currentlesson);
    ui->btnStart->setVisible(true);
}

void Widget::on_sldDuration_valueChanged(int value)
{
    qDebug() << "val = " << value;
}
