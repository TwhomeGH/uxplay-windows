// Exercise real asynchronous logging with a deliberately blocked console pipe.
#include "../src/logging.h"
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <cassert>
#include <cstdio>
#include <fcntl.h>
#include <io.h>
#include <thread>

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    QTemporaryDir directory(QDir::currentPath() + "/build/logging-test-XXXXXX");
    assert(directory.isValid());
    directory.setAutoRemove(false); // Keep ignored test artifacts for inspection.
    const QString path = directory.path() + "/test.log";
    {
        QFile previous(path);
        assert(previous.open(QIODevice::WriteOnly));
        assert(previous.write(QByteArray(8 * 1024 * 1024, 'x')) == 8 * 1024 * 1024);
    }
    int pipeHandles[2];
    assert(_pipe(pipeHandles, 4096, _O_BINARY) == 0);
    const int originalConsole = _dup(2);
    assert(originalConsole >= 0);
    assert(_dup2(pipeHandles[1], 2) == 0);
    _close(pipeHandles[1]);
    applog::install(path);
    std::thread producers[4];
    for (int producer = 0; producer < 4; ++producer) {
        producers[producer] = std::thread([producer] {
            for (int i = 0; i < 10000; ++i)
                qInfo().noquote() << producer << i << QString(1024, QLatin1Char('x'));
        });
    }
    for (auto &producer : producers) producer.join();
    // Producers finished although the consumer is blocked by the unread pipe.
    const auto loaded = applog::stats();
    assert(loaded.queuedBytes <= 4 * 1024 * 1024);
    assert(loaded.queuedLines <= 4096 && loaded.droppedLines > 0);
    assert(!applog::flush(20));

    QByteArray console;
    std::thread reader([&] {
        char bytes[8192];
        int size;
        while ((size = _read(pipeHandles[0], bytes, sizeof(bytes))) > 0)
            console.append(bytes, size);
    });
    qCritical("critical-message-survives-overload");
    assert(applog::flush(10000));
    qInfo("shutdown-tail-marker");
    applog::shutdown();
    assert(_dup2(originalConsole, 2) == 0);
    _close(originalConsole);
    reader.join();
    _close(pipeHandles[0]);
    assert(console.contains("queue full: dropped"));
    assert(console.contains("critical-message-survives-overload"));
    QFile log(path);
    assert(log.open(QIODevice::ReadOnly));
    const QByteArray written = log.readAll();
    assert(written.contains("shutdown-tail-marker"));
    assert(written.contains("critical-message-survives-overload"));
    assert(QFile::exists(directory.path() + "/test.1.log"));
    assert(applog::stats().writtenBatches < 100);
    // A new install/shutdown cycle must start and join a fresh writer cleanly.
    applog::install(path);
    qInfo("reinstalled-marker");
    assert(applog::flush());
    applog::shutdown();
    applog::shutdown();
    puts("Async logging tests passed: blocked console, bounded overload, drain, rotation, reinstall");
}
