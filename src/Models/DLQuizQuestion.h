#ifndef DLQUIZQUESTION_H
#define DLQUIZQUESTION_H

#include <QString>
#include <QStringList>

struct DLQuizQuestion
{
    QString wordId;
    QString quizType;
    QString instruction;
    QString prompt;
    QString germanWord;
    QString nativeTranslation;
    QString exampleDe;
    QString exampleNative;
    QString answer;
    QStringList options;
    QString selectedAnswer;
    bool isAnswered = false;
    bool isCorrect = false;
    QString feedback;
};

#endif // DLQUIZQUESTION_H
