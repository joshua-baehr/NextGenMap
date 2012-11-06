#include "SAMWriter.h"

#include <string.h>

#include "Config.h"
#include "SequenceProvider.h"
#include "NGM.h"

//Format: http://samtools.sourceforge.net/SAM1.pdf
static int const report_offset = 1;

void SAMWriter::DoWriteProlog() {
	//TODO: check correct format

	char const * refName = 0;
	int refNameLength = 0;
	Print("@HD\tVN:1.0\tSO:unsorted\n");

	int ref = Config.GetInt("ref_mode");

	if (ref == -1)
		for (int i = 0; i < SequenceProvider.GetRefCount(); ++i) {

			refName = SequenceProvider.GetRefName(i, refNameLength);
			Print("@SQ\tSN:%.*s\tLN:%d\n", refNameLength, refName, SequenceProvider.GetRefLen(i));
			if (NGM.DualStrand())
				++i;
		}
	else {
		refName = SequenceProvider.GetRefName(ref * ((NGM.DualStrand()) ? 2 : 1), refNameLength);
		Print("@SQ\tSN:%.*s\tLN:%i\n", refNameLength, refName, SequenceProvider.GetRefLen(ref * ((NGM.DualStrand()) ? 2 : 1)));
	}

	//TODO: add version
	Print("@PG\tID:ngm\tVN:%s\tCL:\"%s\"\n", "0.0.1", Config.GetString("cmdline"));

}

void SAMWriter::DoWriteRead(MappedRead const * const read) {
	DoWriteReadGeneric(read, "*", -1, 0, read->mappingQlty);
}

void SAMWriter::DoWriteReadGeneric(MappedRead const * const read, char const * pRefName, int const pLoc, int const pDist,
		int const mappingQlty, int flags) {

	static bool const hardClip = Config.GetInt("hard_clip", 0, 1) == 1 || Config.GetInt("silent_clip", 0, 1) == 1;

	char const * readseq = read->Seq;
	//int readlen = read->length;

	char * readname = read->name;
	int readnamelen = read->nameLength;

	char * qltystr = read->qlty;

	if ((read->Strand == '-')) {
		readseq = read->RevSeq;
		flags |= 0x10;
	}

	int refnamelen = 0;
	char const * refname = SequenceProvider.GetRefName(read->TLS()->Location.m_RefId, refnamelen);

	//mandatory fields
	Print("%.*s\t", readnamelen, readname);
	Print("%d\t", flags);
	Print("%.*s\t", refnamelen, refname);
	Print("%u\t", read->TLS()->Location.m_Location + report_offset);
	Print("%d\t", mappingQlty);

	Print("%s\t", read->Buffer1);
	Print("%s\t", pRefName); //Ref. name of the mate/next fragment
	Print("%u\t", pLoc + report_offset); //Position of the mate/next fragment
	Print("%d\t", pDist); //observed Template LENgth

	if (hardClip)
		Print("%.*s\t", read->length - read->QStart - read->QEnd, readseq + read->QStart);
	else
		Print("%.*s\t", read->length, readseq);

	if (qltystr != 0) {
		if (hardClip)
			Print("%.*s\t", read->length - read->QStart - read->QEnd, qltystr + read->QStart);
		else
			Print("%.*s\t", read->length, qltystr);
	} else {
		Print("*\t");
	}

	//Optional fields
	Print("AS:i:%d\t", (int) read->TLS()->Score.f);
	Print("NM:i:%d\t", read->NM);

	if (Config.GetInt("bs_mapping") == 1) {
		if (!(read->ReadId & 1)) {
			if (read->Strand == '-') {
				Print("ZS:Z:%s\t", "-+");
			} else {
				Print("ZS:Z:%s\t", "++");
			}
		} else {
			if (read->Strand == '-') {
				Print("ZS:Z:%s\t", "+-");
			} else {
				Print("ZS:Z:%s\t", "--");
			}
		}
	}

	Print("XI:f:%f\t", read->Identity);
	Print("X0:i:%d\t", read->EqualScoringCount);
	Print("X1:i:%d\t", read->Calculated - read->EqualScoringCount);
	Print("XE:i:%d\t", (int) read->s);
	Print("XR:i:%d\t", read->length - read->QStart - read->QEnd);
	Print("MD:Z:%s", read->Buffer2);

	Print("\n");

}

void SAMWriter::DoWritePair(MappedRead const * const read1, MappedRead const * const read2) {
	//Proper pair
	int flags1 = 0x1;
	int flags2 = 0x1;
	if (read1->ReadId & 0x1) {
		//if read1 is the second pair
		flags1 |= 0x80;
		flags2 |= 0x40;
	} else {
		//if read1 is the first pair
		flags1 |= 0x40;
		flags2 |= 0x80;
	}
	if (!read1->hasCandidates() && !read2->hasCandidates()) {
		//Both mates unmapped
		DoWriteUnmappedRead(read2, flags2 | 0x8);
		DoWriteUnmappedRead(read1, flags1 | 0x8);
	} else if (!read1->hasCandidates()) {
		//First mate unmapped
		DoWriteReadGeneric(read2, "=", read2->TLS()->Location.m_Location, 0, read2->mappingQlty, flags2 | 0x8);
		DoWriteUnmappedReadGeneric(read1, read2->TLS()->Location.m_RefId, '=', read2->TLS()->Location.m_Location,
				read2->TLS()->Location.m_Location, 0, 0, flags1);
	} else if (!read2->hasCandidates()) {
		//Second mate unmapped
		DoWriteUnmappedReadGeneric(read2, read1->TLS()->Location.m_RefId, '=', read1->TLS()->Location.m_Location,
				read1->TLS()->Location.m_Location, 0, 0, flags2);
		DoWriteReadGeneric(read1, "=", read1->TLS()->Location.m_Location, 0, read1->mappingQlty, flags1 | 0x8);
	} else {
		if (!read1->HasFlag(NGMNames::PairedFail)) {
			//TODO: Check if correct!
			int distance = 0;
			flags1 |= 0x2;
			flags2 |= 0x2;
			if (read1->Strand == '+') {
				distance = read2->TLS()->Location.m_Location + read2->length - read1->TLS()->Location.m_Location;
				DoWriteReadGeneric(read2, "=", read1->TLS()->Location.m_Location, distance * -1, read2->mappingQlty, flags2);
				DoWriteReadGeneric(read1, "=", read2->TLS()->Location.m_Location, distance, read1->mappingQlty, flags1 | 0x20);
			} else if (read2->Strand == '+') {
				distance = read1->TLS()->Location.m_Location + read1->length - read2->TLS()->Location.m_Location;

				DoWriteReadGeneric(read2, "=", read1->TLS()->Location.m_Location, distance, read2->mappingQlty, flags2 | 0x20);
				DoWriteReadGeneric(read1, "=", read2->TLS()->Location.m_Location, distance * -1, read1->mappingQlty, flags1);
			}
		} else {
			int distance = 0;
			if (read1->Strand == '-') {
				flags2 |= 0x20;
			}
			if (read2->Strand == '-') {
				flags1 |= 0x20;
			}
			DoWriteReadGeneric(read2, SequenceProvider.GetRefName(read1->TLS()->Location.m_RefId, distance),
					read1->TLS()->Location.m_Location, 0, read2->mappingQlty, flags2);
			DoWriteReadGeneric(read1, SequenceProvider.GetRefName(read2->TLS()->Location.m_RefId, distance),
					read2->TLS()->Location.m_Location, 0, read1->mappingQlty, flags1);

			//DoWriteRead(read2);
			//DoWriteRead(read1);
			//DoWriteUnmappedRead(read2);
			//DoWriteUnmappedRead(read1);
		}
	}

}

void SAMWriter::DoWriteUnmappedReadGeneric(MappedRead const * const read, int const refId, char const pRefName, int const loc,
		int const pLoc, int const pDist, int const mappingQlty, int flags = 0) {
	//SRR002320.10000027.1    4       *       0       0       *       *       0       0       TTTATGTTGTTAATGTGTTGGGTGAGTGCGCCCCAT    IIIIIIIIIIIIIIIIIIIIIIIIII

	NGM.AddUnmappedRead(read, MFAIL_NOCAND);

	flags |= 0x4;

	char const * readseq = read->Seq;
	int readlen = read->length;

	char * readname = read->name;
	int readnamelen = read->nameLength;

	char * qltystr = read->qlty;
	int qltylen = readlen;

	//mandatory fields
	Print("%.*s\t", readnamelen, readname);
	Print("%d\t", flags);
	if (refId > -1) {
		int refnamelen = 0;
		char const * refname = SequenceProvider.GetRefName(refId, refnamelen);
		Print("%.*s\t", refnamelen, refname);
	} else {
		Print("*\t");
	}
	//if(loc > 0) {
	Print("%d\t", loc + report_offset);
	//} else {
	//	Print("0\t");
	//}

	Print("0\t");
	Print("*\t");
	Print("%c\t", pRefName); //Ref. name of the mate/next fragment
	//if(loc > 0) {
	Print("%d\t", pLoc + report_offset); //Position of the mate/next fragment
	//} else {
	//	Print("0\t");
	//}
	Print("%d\t", pDist); //observed Template LENgth
	Print("%s\t", readseq);
	if (qltystr != 0) {
		Print("%.*s", qltylen, qltystr);
	} else {
		Print("*");
	}

	//Optional fields
	//Print("AS:i:0");
	//Print("\tMD:Z:");
	//Print("\tX0:i:0");
	//Print("\tXI:f:0.0");
	//Print("\tXX:i:%d", read->Calculated);

	Print("\n");
}

void SAMWriter::DoWriteUnmappedRead(MappedRead const * const read, int flags) {
	DoWriteUnmappedReadGeneric(read, -1, '*', -1, -1, 0, 0, flags | 0x04);
}

void SAMWriter::DoWriteEpilog() {

}