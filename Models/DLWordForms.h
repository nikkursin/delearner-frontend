#ifndef DLWORDFORMS_H
#define DLWORDFORMS_H

#include <QString>

struct DLNounForms
{
    QString pluralForm;
};

struct DLVerbForms
{
    QString praeteritumForm;
    QString partizipIIForm;
};

struct DLAdjectiveForms
{
    QString positiveForm;
    QString comparativeForm;
    QString superlativeForm;
};

#endif // DLWORDFORMS_H
