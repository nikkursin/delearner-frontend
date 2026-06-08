#ifndef DLQUIZRESULT_H
#define DLQUIZRESULT_H

#include <QString>

struct DLQuizResult
{
    QString quizType;
    QString quizTitle;
    int total = 0;
    int answered = 0;
    int correct = 0;
    int wrong = 0;
    int accuracy = 0;
    int groupId = -1;
};

#endif // DLQUIZRESULT_H
